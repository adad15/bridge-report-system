#include <optional>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ImportBindingRepository.hpp"
#include "bridge_report/db/ComponentRangeSplitRepository.hpp"

namespace {

using bridge_report::db::BindingOverview;
using bridge_report::db::BindingRow;
using bridge_report::db::BindingStatus;
using bridge_report::db::ImportBindingRepository;

const BindingRow* find_row(const BindingOverview& overview, const std::string& part_name,
                           const std::string& number) {
    for (const auto& group : overview.groups) {
        if (group.part_name != part_name) continue;
        for (const auto& row : group.rows) {
            if (row.component_number == number) return &row;
        }
    }
    return nullptr;
}

class ImportBindingRepositoryTest : public testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL is not set";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{});
        user_id_ = client_->execSqlSync(
            "select id::text from users where username='admin'")[0]["id"].as<std::string>();
        bridge_id_ = client_->execSqlSync(
            "insert into bridges(bridge_name) values('绑定测试桥') returning id::text")[0]["id"]
                .as<std::string>();
        year_id_ = client_->execSqlSync(
            "insert into inspection_years(bridge_id,inspection_year,status,is_current) "
            "values($1::uuid,2026,'待校对',true) returning id::text",
            bridge_id_)[0]["id"].as<std::string>();
        package_id_ = client_->execSqlSync(
            "insert into standard_packages(standard_family,standard_id,standard_code,standard_name,"
            "official_edition,package_version,contract_version,algorithm_id,effective_date,content_checksum) "
            "values('technical_condition','BIND-'||gen_random_uuid()::text,'BIND','绑定规范','2026',"
            "'1.0.0',1,'bind','2026-01-01','sha256:'||repeat('c',64)) returning id::text")[0]["id"]
                .as<std::string>();

        // 已确认台账：一个上部承重构件 1-1#梁。
        component_id_ = client_->execSqlSync(
            "insert into bridge_components(bridge_id,structure_part,component_type,business_component_code,"
            "normalized_component_key,current_status,creation_source) "
            "values($1::uuid,'上部结构','空心板','1-1#梁','bind-key-1','已确认','人工录入') returning id::text",
            bridge_id_)[0]["id"].as<std::string>();
        // 先建草稿版本 + 条目 + 映射，再确认（已确认版本的条目不可变）。
        const auto revision_id = client_->execSqlSync(
            "insert into bridge_component_inventory_revisions(bridge_id,revision_number,created_by_user_id) "
            "values($1::uuid,1,$2::uuid) returning id::text",
            bridge_id_, user_id_)[0]["id"].as<std::string>();
        const auto entry_id = client_->execSqlSync(
            "insert into bridge_component_inventory_entries(inventory_revision_id,bridge_component_id,"
            "component_number,site_name,site_component_type,sort_order) "
            "values($1::uuid,$2::uuid,'1-1#梁','空心板','空心板',1) returning id::text",
            revision_id, component_id_)[0]["id"].as<std::string>();
        client_->execSqlSync(
            "insert into bridge_component_standard_mappings(inventory_entry_id,standard_package_id,"
            "standard_bridge_type_id,standard_component_category_id,structure_part,mapping_source,"
            "confirmation_status,confirmed_by_user_id,confirmed_at) "
            "values($1::uuid,$2::uuid,'h21.bridge_type.beam','h21.component.beam.upper_bearing',"
            "'superstructure','规范模板','已确认',$3::uuid,now())",
            entry_id, package_id_, user_id_);
        client_->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认',"
            "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
            revision_id, user_id_);

        // 待校对导入记录：3 条病害引用 1-1#梁（未匹配）+ 1 条支座。
        Json::Value parsed;
        parsed["defects"] = Json::Value(Json::arrayValue);
        for (int i = 0; i < 3; ++i) {
            Json::Value defect;
            defect["candidate_id"] = "d" + std::to_string(i);
            defect["component_name"] = "上部承重构件";
            defect["component_number"] = "1-1#梁";
            parsed["defects"].append(defect);
        }
        Json::Value bearing;
        bearing["candidate_id"] = "d3";
        bearing["component_name"] = "支座";
        bearing["component_number"] = "2-1#支座";
        parsed["defects"].append(bearing);
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        import_id_ = client_->execSqlSync(
            "insert into import_records(bridge_id,inspection_year_id,import_name,source_type,import_status,"
            "parsed_result_json) values($1::uuid,$2::uuid,'绑定导入','软件导出Word','待校对',$3::jsonb) "
            "returning id::text",
            bridge_id_, year_id_, Json::writeString(builder, parsed))[0]["id"].as<std::string>();
    }

    void TearDown() override {
        if (!client_) return;
        client_->execSqlSync("delete from import_records where id=$1::uuid", import_id_);
        client_->execSqlSync("delete from inspection_years where id=$1::uuid", year_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from standard_packages where id=$1::uuid", package_id_);
        client_->closeAll();
    }

    drogon::orm::DbClientPtr client_;
    std::string user_id_, bridge_id_, year_id_, package_id_, component_id_, import_id_;
};

TEST_F(ImportBindingRepositoryTest, OverviewGroupsByPartNameAndCountsReferences) {
    ImportBindingRepository repository(client_);
    const auto outcome = repository.overview(import_id_);
    ASSERT_EQ(outcome.status, BindingStatus::Ok);
    ASSERT_TRUE(outcome.overview.has_value());
    EXPECT_TRUE(outcome.overview->inventory_confirmed);

    const auto* girder = find_row(*outcome.overview, "上部承重构件", "1-1#梁");
    ASSERT_NE(girder, nullptr);
    EXPECT_EQ(girder->defect_count, 3);
    EXPECT_EQ(girder->status, "unmatched");
    const auto* bearing = find_row(*outcome.overview, "支座", "2-1#支座");
    ASSERT_NE(bearing, nullptr);
    EXPECT_EQ(bearing->defect_count, 1);
}

TEST_F(ImportBindingRepositoryTest, PreviewAndApplySelectedRangeAtomically) {
    const auto stored = client_->execSqlSync(
        "select parsed_result_json::text as parsed from import_records where id=$1::uuid",
        import_id_);
    Json::Value parsed;
    Json::CharReaderBuilder reader_builder;
    std::string errors;
    const auto parsed_text = stored[0]["parsed"].as<std::string>();
    const std::unique_ptr<Json::CharReader> reader(reader_builder.newCharReader());
    ASSERT_TRUE(reader->parse(
        parsed_text.data(),
        parsed_text.data() + parsed_text.size(),
        &parsed, &errors));
    for (Json::ArrayIndex i = 0; i < 3; ++i) {
        parsed["defects"][i]["component_number"] = "1-1#梁~1-25#梁";
    }
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    client_->execSqlSync(
        "update import_records set parsed_result_json=$2::jsonb where id=$1::uuid",
        import_id_, Json::writeString(writer, parsed));

    bridge_report::db::ComponentRangeSplitRepository repository(client_);
    const std::vector<bridge_report::review::ComponentRangeSplitTarget> targets{
        {"上部承重构件", "1-1#梁~1-25#梁"}};
    const auto preview = repository.preview(import_id_, targets);
    ASSERT_EQ(preview.status, bridge_report::db::ComponentRangeSplitStatus::Ok);
    ASSERT_TRUE(preview.plan.has_value());
    EXPECT_EQ(preview.plan->totals.result_defect_count, 75);
    EXPECT_EQ(preview.plan->totals.bound_count, 3);
    EXPECT_TRUE(preview.impact_token.starts_with("sha256:"));

    const auto applied =
        repository.apply(import_id_, targets, preview.impact_token, user_id_);
    ASSERT_EQ(applied.status, bridge_report::db::ComponentRangeSplitStatus::Ok);
    ASSERT_TRUE(applied.overview.has_value());
    EXPECT_FALSE(applied.operation_id.empty());
    const auto after = client_->execSqlSync(
        "select jsonb_array_length(parsed_result_json->'defects') as count,"
        "parsed_result_json#>>'{defects,0,range_split_origin,operated_by_user_id}' as actor "
        "from import_records where id=$1::uuid", import_id_);
    EXPECT_EQ(after[0]["count"].as<int>(), 76);
    EXPECT_EQ(after[0]["actor"].as<std::string>(), user_id_);

    const auto replay =
        repository.apply(import_id_, targets, preview.impact_token, user_id_);
    EXPECT_NE(replay.status, bridge_report::db::ComponentRangeSplitStatus::Ok);
}

TEST_F(ImportBindingRepositoryTest, BindAttachesAllReferencingDefects) {
    ImportBindingRepository repository(client_);
    const auto outcome = repository.bind(import_id_, "上部承重构件", "1-1#梁", component_id_);
    ASSERT_EQ(outcome.status, BindingStatus::Ok) << static_cast<int>(outcome.status);
    const auto* row = find_row(*outcome.overview, "上部承重构件", "1-1#梁");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->status, "bound");
    ASSERT_TRUE(row->bridge_component_id.has_value());
    EXPECT_EQ(*row->bridge_component_id, component_id_);
    EXPECT_EQ(row->defect_count, 3);
}

TEST_F(ImportBindingRepositoryTest, BindRejectsCategoryMismatch) {
    ImportBindingRepository repository(client_);
    // 把支座行绑到上部承重构件构件 → 类别不符。
    const auto outcome = repository.bind(import_id_, "支座", "2-1#支座", component_id_);
    EXPECT_EQ(outcome.status, BindingStatus::Conflict);
}

TEST_F(ImportBindingRepositoryTest, MarkMissingAndClearRoundTrip) {
    ImportBindingRepository repository(client_);
    const auto missing = repository.mark_missing(import_id_, "支座", "2-1#支座");
    ASSERT_EQ(missing.status, BindingStatus::Ok);
    EXPECT_EQ(find_row(*missing.overview, "支座", "2-1#支座")->status, "missing");

    const auto bound = repository.bind(import_id_, "上部承重构件", "1-1#梁", component_id_);
    ASSERT_EQ(bound.status, BindingStatus::Ok);
    const auto cleared = repository.clear(import_id_, "上部承重构件", "1-1#梁");
    ASSERT_EQ(cleared.status, BindingStatus::Ok);
    EXPECT_EQ(find_row(*cleared.overview, "上部承重构件", "1-1#梁")->status, "unmatched");
}

TEST_F(ImportBindingRepositoryTest, BindBatchAppliesEveryTarget) {
    ImportBindingRepository repository(client_);
    const auto outcome = repository.bind_batch(import_id_, {
        {"上部承重构件", "1-1#梁", component_id_},
    });
    ASSERT_EQ(outcome.status, BindingStatus::Ok) << static_cast<int>(outcome.status);
    ASSERT_TRUE(outcome.overview.has_value());
    const auto* row = find_row(*outcome.overview, "上部承重构件", "1-1#梁");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->status, "bound");
}

// 整批原子：任一目标非法则一条都不写，否则用户无从判断哪些生效了。
TEST_F(ImportBindingRepositoryTest, BindBatchWritesNothingWhenAnyTargetIsInvalid) {
    ImportBindingRepository repository(client_);
    const auto outcome = repository.bind_batch(import_id_, {
        {"上部承重构件", "1-1#梁", component_id_},   // 合法
        {"支座", "2-1#支座", component_id_},          // 类别不符
    });
    EXPECT_EQ(outcome.status, BindingStatus::Conflict);
    EXPECT_EQ(outcome.rejected_component_number, "2-1#支座");

    // 合法的那条也不得写入。
    const auto after = repository.overview(import_id_);
    ASSERT_EQ(after.status, BindingStatus::Ok);
    EXPECT_EQ(find_row(*after.overview, "上部承重构件", "1-1#梁")->status, "unmatched");
}

TEST_F(ImportBindingRepositoryTest, BindBatchRejectsUnknownComponentNumber) {
    ImportBindingRepository repository(client_);
    const auto outcome = repository.bind_batch(import_id_, {
        {"上部承重构件", "9-9#不存在", component_id_},
    });
    EXPECT_EQ(outcome.status, BindingStatus::Invalid);
    EXPECT_EQ(outcome.rejected_component_number, "9-9#不存在");
}

TEST_F(ImportBindingRepositoryTest, BindBatchRejectsEmptyTargets) {
    ImportBindingRepository repository(client_);
    EXPECT_EQ(repository.bind_batch(import_id_, {}).status, BindingStatus::Invalid);
}

TEST_F(ImportBindingRepositoryTest, RejectsWritesOutsidePendingReview) {
    client_->execSqlSync("update import_records set import_status='已确认' where id=$1::uuid", import_id_);
    ImportBindingRepository repository(client_);
    EXPECT_EQ(repository.overview(import_id_).status, BindingStatus::Conflict);
    EXPECT_EQ(repository.bind(import_id_, "上部承重构件", "1-1#梁", component_id_).status,
              BindingStatus::Conflict);
    EXPECT_EQ(repository.mark_missing(import_id_, "支座", "2-1#支座").status, BindingStatus::Conflict);
    EXPECT_EQ(repository.bind_batch(import_id_, {{"上部承重构件", "1-1#梁", component_id_}}).status,
              BindingStatus::Conflict);
}

}  // namespace
