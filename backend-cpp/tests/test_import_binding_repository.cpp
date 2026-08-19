#include <optional>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/EditLockRepository.hpp"
#include "bridge_report/db/ImportBindingRepository.hpp"
#include "bridge_report/db/ComponentRangeSplitRepository.hpp"

namespace {

using bridge_report::db::BindingOutcome;
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
        revision_id_ = client_->execSqlSync(
            "insert into bridge_component_inventory_revisions(bridge_id,revision_number,created_by_user_id) "
            "values($1::uuid,1,$2::uuid) returning id::text",
            bridge_id_, user_id_)[0]["id"].as<std::string>();
        const auto entry_id = client_->execSqlSync(
            "insert into bridge_component_inventory_entries(inventory_revision_id,bridge_component_id,"
            "component_number,site_name,site_component_type,sort_order) "
            "values($1::uuid,$2::uuid,'1-1#梁','空心板','空心板',1) returning id::text",
            revision_id_, component_id_)[0]["id"].as<std::string>();
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
            revision_id_, user_id_);

        // 待校对导入记录：3 条病害引用 1-1#梁（未匹配）+ 1 条支座。
        Json::Value parsed;
        parsed["defects"] = Json::Value(Json::arrayValue);
        for (int i = 0; i < 3; ++i) {
            Json::Value defect;
            defect["candidate_id"] = "d" + std::to_string(i);
            defect["component_name"] = "上部承重构件";
            defect["component_number"] = "1-1#梁";
            defect["warnings"] = Json::Value(Json::arrayValue);
            Json::Value match_warning(Json::objectValue);
            match_warning["code"] = "defect_component_match_required";
            match_warning["message"] = "未匹配";
            match_warning["severity"] = "warning";
            defect["warnings"].append(match_warning);
            if (i == 0) {
                Json::Value scale_warning(Json::objectValue);
                scale_warning["code"] = "defect_scale_invalid";
                scale_warning["message"] = "标度异常";
                scale_warning["severity"] = "warning";
                defect["warnings"].append(scale_warning);
            }
            parsed["defects"].append(defect);
        }
        Json::Value bearing;
        bearing["candidate_id"] = "d3";
        bearing["component_name"] = "支座";
        bearing["component_number"] = "2-1#支座";
        bearing["warnings"] = Json::Value(Json::arrayValue);
        Json::Value bearing_warning(Json::objectValue);
        bearing_warning["code"] = "defect_component_match_required";
        bearing_warning["message"] = "未匹配";
        bearing_warning["severity"] = "warning";
        bearing["warnings"].append(bearing_warning);
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

    // 在本桥另开一个版本。草稿版本用来复现"草稿优先排序"，已确认版本用来验证年度锁定
    // 版本优先于桥梁最新版本。返回新版本 id。
    std::string add_revision(int revision_number, bool confirmed) {
        const auto id = client_->execSqlSync(
            "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
            "baseline_revision_id,created_by_user_id) values($1::uuid,$2,$3::uuid,$4::uuid) "
            "returning id::text",
            bridge_id_, revision_number, revision_id_, user_id_)[0]["id"].as<std::string>();
        // 只放一个与 1-1#梁 无关的构件：谁被选中一目了然——取到这个版本就找不到 1-1#梁。
        const auto entry_id = client_->execSqlSync(
            "insert into bridge_component_inventory_entries(inventory_revision_id,"
            "bridge_component_id,component_number,site_name,site_component_type,sort_order) "
            "values($1::uuid,$2::uuid,'9-9#梁','空心板','空心板',1) returning id::text",
            id, component_id_)[0]["id"].as<std::string>();
        client_->execSqlSync(
            "insert into bridge_component_standard_mappings(inventory_entry_id,standard_package_id,"
            "standard_bridge_type_id,standard_component_category_id,structure_part,mapping_source,"
            "confirmation_status,confirmed_by_user_id,confirmed_at) "
            "values($1::uuid,$2::uuid,'h21.bridge_type.beam','h21.component.beam.upper_bearing',"
            "'superstructure','规范模板','已确认',$3::uuid,now())",
            entry_id, package_id_, user_id_);
        if (confirmed) {
            client_->execSqlSync(
                "update bridge_component_inventory_revisions set status='已确认',"
                "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
                id, user_id_);
        }
        return id;
    }

    // 把 1-1#梁 的三条病害改写成区间编号，供范围拆分用例使用。
    void make_defects_a_range() {
        const auto stored = client_->execSqlSync(
            "select parsed_result_json::text as parsed from import_records where id=$1::uuid",
            import_id_);
        Json::Value parsed;
        Json::CharReaderBuilder reader_builder;
        std::string errors;
        const auto parsed_text = stored[0]["parsed"].as<std::string>();
        const std::unique_ptr<Json::CharReader> reader(reader_builder.newCharReader());
        ASSERT_TRUE(reader->parse(parsed_text.data(),
                                  parsed_text.data() + parsed_text.size(), &parsed, &errors));
        for (Json::ArrayIndex i = 0; i < 3; ++i) {
            parsed["defects"][i]["component_number"] = "1-1#梁~1-25#梁";
        }
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        client_->execSqlSync(
            "update import_records set parsed_result_json=$2::jsonb where id=$1::uuid",
            import_id_, Json::writeString(writer, parsed));
    }

    drogon::orm::DbClientPtr client_;
    std::string user_id_, bridge_id_, year_id_, package_id_, component_id_, revision_id_, import_id_;
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
    make_defects_a_range();

    bridge_report::db::ComponentRangeSplitRepository repository(client_);
    const std::vector<bridge_report::review::ComponentRangeSplitTarget> targets{
        {"上部承重构件", "1-1#梁~1-25#梁"}};
    const auto preview = repository.preview(import_id_, targets, revision_id_);
    ASSERT_EQ(preview.status, bridge_report::db::ComponentRangeSplitStatus::Ok);
    ASSERT_TRUE(preview.analysis.has_value());
    EXPECT_FALSE(preview.plan.has_value());
    EXPECT_EQ(preview.analysis->totals.result_defect_count, 75);
    EXPECT_EQ(preview.analysis->totals.bound_count, 3);
    EXPECT_TRUE(preview.impact_token.starts_with("sha256:"));

    const auto applied =
        repository.apply(import_id_, targets, preview.impact_token, user_id_, revision_id_);
    ASSERT_EQ(applied.status, bridge_report::db::ComponentRangeSplitStatus::Ok);
    ASSERT_TRUE(applied.analysis.has_value());
    ASSERT_TRUE(applied.plan.has_value());
    ASSERT_TRUE(applied.overview.has_value());
    EXPECT_FALSE(applied.operation_id.empty());
    const auto after = client_->execSqlSync(
        "select jsonb_array_length(parsed_result_json->'defects') as count,"
        "parsed_result_json#>>'{defects,0,range_split_origin,operated_by_user_id}' as actor "
        "from import_records where id=$1::uuid", import_id_);
    EXPECT_EQ(after[0]["count"].as<int>(), 76);
    EXPECT_EQ(after[0]["actor"].as<std::string>(), user_id_);

    const auto replay =
        repository.apply(import_id_, targets, preview.impact_token, user_id_, revision_id_);
    EXPECT_NE(replay.status, bridge_report::db::ComponentRangeSplitStatus::Ok);
}

// 版本解析一度走 get_latest_revision()，那条排序草稿优先，于是桥上只要有一个草稿就先
// 取到草稿，随后"是否已确认"的判断必然不成立——绑定面板整块被判成"台账未确认"。
TEST_F(ImportBindingRepositoryTest, OverviewStaysConfirmedWhileADraftExists) {
    add_revision(2, /*confirmed=*/false);

    ImportBindingRepository repository(client_);
    const auto outcome = repository.overview(import_id_);
    ASSERT_EQ(outcome.status, BindingStatus::Ok);
    ASSERT_TRUE(outcome.overview.has_value());
    EXPECT_TRUE(outcome.overview->inventory_confirmed);
}

// 概览要自带可显示的构件信息，前端才不必为把 id 换成编号去拉整份台账。
TEST_F(ImportBindingRepositoryTest, OverviewCarriesDisplayableComponents) {
    ImportBindingRepository repository(client_);
    ASSERT_EQ(repository.bind(import_id_, "上部承重构件", "1-1#梁", component_id_,
                              revision_id_).status,
              BindingStatus::Ok);

    const auto outcome = repository.overview(import_id_);
    ASSERT_EQ(outcome.status, BindingStatus::Ok);
    const auto* row = find_row(*outcome.overview, "上部承重构件", "1-1#梁");
    ASSERT_NE(row, nullptr);
    ASSERT_TRUE(row->bound_component.has_value());
    EXPECT_EQ(row->bound_component->component_number, "1-1#梁");
    EXPECT_EQ(row->bound_component->site_component_type, "空心板");
    EXPECT_EQ(row->bound_component->site_name, "空心板");
    // entry_id 是下拉 <option> 的 key，不能省。
    EXPECT_FALSE(row->bound_component->entry_id.empty());
}

// 展示对象只填"启用且有生效映射"的构件，复现旧前端 usableEntries() 的可见范围。
// 绑的构件在当前版本里查不到时（历史绑定、构件已从台账移除），旧代码走的是
// "已绑定构件"那条文案分支，而不是显示编号。
TEST_F(ImportBindingRepositoryTest, OverviewOmitsBoundComponentOutsideTheRevision) {
    // 一个不在本版本台账里的构件；直接写进 parsed_result_json，绕开 bind 的校验。
    const auto orphan_id = client_->execSqlSync(
        "insert into bridge_components(bridge_id,structure_part,component_type,"
        "business_component_code,normalized_component_key,current_status,creation_source) "
        "values($1::uuid,'上部结构','空心板','9-1#梁','bind-key-orphan','已确认','人工录入') "
        "returning id::text",
        bridge_id_)[0]["id"].as<std::string>();
    client_->execSqlSync(
        "update import_records set parsed_result_json=jsonb_set("
        "parsed_result_json,'{defects,0,bridge_component_id}',to_jsonb($2::text)) "
        "where id=$1::uuid",
        import_id_, orphan_id);

    ImportBindingRepository repository(client_);
    const auto outcome = repository.overview(import_id_);
    const auto* row = find_row(*outcome.overview, "上部承重构件", "1-1#梁");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->status, "bound") << "行状态不受展示对象缺失影响";
    EXPECT_FALSE(row->bound_component.has_value());
}

// 展示对象缺失只影响显示：内部候选 id 还在，行就还是 ambiguous。
TEST_F(ImportBindingRepositoryTest, MissingSummariesDoNotChangeRowStatus) {
    // 造一条带两个候选的病害，其中一个 id 在台账里根本不存在。
    client_->execSqlSync(
        "update import_records set parsed_result_json=jsonb_set("
        "parsed_result_json,'{defects,3,component_match_candidate_ids}',"
        "$2::jsonb) where id=$1::uuid",
        import_id_,
        "[\"" + component_id_ + "\",\"11111111-1111-1111-1111-111111111111\"]");

    ImportBindingRepository repository(client_);
    const auto outcome = repository.overview(import_id_);
    const auto* row = find_row(*outcome.overview, "支座", "2-1#支座");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->status, "ambiguous");
    EXPECT_EQ(row->candidate_component_ids.size(), 2u);
    // 只有真实存在且可绑的那一个能显示出来。
    ASSERT_EQ(row->candidate_components.size(), 1u);
    EXPECT_EQ(row->candidate_components.front().bridge_component_id, component_id_);
}

// 批量替换取数：精简条目、按可绑过滤、只校验不锁定。
TEST_F(ImportBindingRepositoryTest, ReplaceInventoryReturnsTrimmedBindableEntries) {
    ImportBindingRepository repository(client_);
    const auto outcome = repository.load_replace_inventory(import_id_, revision_id_);
    ASSERT_EQ(outcome.status, BindingStatus::Ok);
    EXPECT_EQ(outcome.replace_revision_id, revision_id_);
    ASSERT_EQ(outcome.replace_entries.size(), 1u);
    EXPECT_EQ(outcome.replace_entries.front().component_number, "1-1#梁");
    EXPECT_EQ(outcome.replace_entries.front().bridge_component_id, component_id_);
    // 服务端已按 is_active 过滤，字段仍必须带回：前端预览里 !entry.is_active 会跳过，
    // 字段缺失时 !undefined 为真，整批安静地判成"台账里没有"。
    EXPECT_TRUE(outcome.replace_entries.front().is_active);

    // 取数不许锁定年度版本。
    const auto year = client_->execSqlSync(
        "select component_inventory_revision_id::text as revision_id "
        "from inspection_years where id=$1::uuid", year_id_);
    EXPECT_TRUE(year[0]["revision_id"].isNull());
}

TEST_F(ImportBindingRepositoryTest, ReplaceInventoryRejectsAStaleExpectedRevision) {
    ImportBindingRepository repository(client_);
    const auto outcome = repository.load_replace_inventory(
        import_id_, "11111111-1111-1111-1111-111111111111");
    EXPECT_EQ(outcome.status, BindingStatus::Conflict);
    EXPECT_EQ(outcome.error_code, "component_inventory_revision_changed");
}

// 前端拿这个 id 当搜索寻址、缓存键和写操作的 expected 版本，缺了整条契约就断了。
TEST_F(ImportBindingRepositoryTest, OverviewCarriesTheResolvedRevisionId) {
    ImportBindingRepository repository(client_);
    const auto outcome = repository.overview(import_id_);
    ASSERT_EQ(outcome.status, BindingStatus::Ok);
    ASSERT_TRUE(outcome.overview->inventory_revision_id.has_value());
    EXPECT_EQ(*outcome.overview->inventory_revision_id, revision_id_);
    // 不变量：有值 当且仅当 inventory_confirmed 为真。
    EXPECT_EQ(outcome.overview->inventory_confirmed,
              outcome.overview->inventory_revision_id.has_value());
}

// 概览是读操作：解析出版本也不许写进年度。否则光是打开面板就把年度锁死了。
TEST_F(ImportBindingRepositoryTest, OverviewNeverLocksTheYearRevision) {
    ImportBindingRepository repository(client_);
    ASSERT_EQ(repository.overview(import_id_).status, BindingStatus::Ok);

    const auto year = client_->execSqlSync(
        "select component_inventory_revision_id::text as revision_id "
        "from inspection_years where id=$1::uuid", year_id_);
    EXPECT_TRUE(year[0]["revision_id"].isNull());
}

// 拆分预览同样是读操作。
TEST_F(ImportBindingRepositoryTest, RangeSplitPreviewNeverLocksTheYearRevision) {
    make_defects_a_range();
    bridge_report::db::ComponentRangeSplitRepository repository(client_);
    const std::vector<bridge_report::review::ComponentRangeSplitTarget> targets{
        {"上部承重构件", "1-1#梁~1-25#梁"}};
    ASSERT_EQ(repository.preview(import_id_, targets, revision_id_).status,
              bridge_report::db::ComponentRangeSplitStatus::Ok);

    const auto year = client_->execSqlSync(
        "select component_inventory_revision_id::text as revision_id "
        "from inspection_years where id=$1::uuid", year_id_);
    EXPECT_TRUE(year[0]["revision_id"].isNull());
}

// 每条写路径都必须挡住版本漂移，且用同一个错误码——前端才能一处接住。
TEST_F(ImportBindingRepositoryTest, WritesRejectAStaleExpectedRevision) {
    const std::string stale = "11111111-1111-1111-1111-111111111111";
    ImportBindingRepository repository(client_);

    const auto expect_changed = [](const BindingOutcome& outcome, const char* what) {
        EXPECT_EQ(outcome.status, BindingStatus::Conflict) << what;
        EXPECT_EQ(outcome.error_code, "component_inventory_revision_changed") << what;
    };
    expect_changed(
        repository.bind(import_id_, "上部承重构件", "1-1#梁", component_id_, stale), "bind");
    expect_changed(
        repository.bind_batch(import_id_, {{"上部承重构件", "1-1#梁", component_id_}}, stale),
        "bind_batch");
    expect_changed(repository.mark_missing(import_id_, "支座", "2-1#支座", stale), "mark_missing");
    expect_changed(repository.clear(import_id_, "上部承重构件", "1-1#梁", stale), "clear");

    // 一条都不许写进去。
    const auto after = repository.overview(import_id_);
    EXPECT_EQ(find_row(*after.overview, "上部承重构件", "1-1#梁")->status, "unmatched");
    EXPECT_EQ(find_row(*after.overview, "支座", "2-1#支座")->status, "unmatched");
}

// 缺陷四：年度未锁版本时，写操作会重新解析并把新版本锁进去，用户毫不知情。
TEST_F(ImportBindingRepositoryTest, MarkMissingRefusesToSwitchToANewerRevision) {
    add_revision(2, /*confirmed=*/true);   // 用户看过概览之后，别人确认了 R2

    ImportBindingRepository repository(client_);
    const auto outcome = repository.mark_missing(import_id_, "支座", "2-1#支座", revision_id_);
    EXPECT_EQ(outcome.status, BindingStatus::Conflict);
    EXPECT_EQ(outcome.error_code, "component_inventory_revision_changed");

    // 年度不能被悄悄锁到 R2 上。
    const auto year = client_->execSqlSync(
        "select component_inventory_revision_id::text as revision_id "
        "from inspection_years where id=$1::uuid", year_id_);
    EXPECT_TRUE(year[0]["revision_id"].isNull());
}

// 拆分预览与应用走同一个错误码；impact_token 与版本校验各管一段，互不替代。
TEST_F(ImportBindingRepositoryTest, RangeSplitRejectsAStaleExpectedRevision) {
    make_defects_a_range();
    bridge_report::db::ComponentRangeSplitRepository repository(client_);
    const std::vector<bridge_report::review::ComponentRangeSplitTarget> targets{
        {"上部承重构件", "1-1#梁~1-25#梁"}};

    const auto good = repository.preview(import_id_, targets, revision_id_);
    ASSERT_EQ(good.status, bridge_report::db::ComponentRangeSplitStatus::Ok);

    const std::string stale = "11111111-1111-1111-1111-111111111111";
    const auto preview = repository.preview(import_id_, targets, stale);
    EXPECT_EQ(preview.status, bridge_report::db::ComponentRangeSplitStatus::Conflict);
    EXPECT_EQ(preview.error_code, "component_inventory_revision_changed");

    // 令牌是对的，只有版本不对：仍须被版本这道闸门挡住。
    const auto applied =
        repository.apply(import_id_, targets, good.impact_token, user_id_, stale);
    EXPECT_EQ(applied.status, bridge_report::db::ComponentRangeSplitStatus::Conflict);
    EXPECT_EQ(applied.error_code, "component_inventory_revision_changed");
}

// 反过来：版本对、令牌过期，必须报令牌那条，不能混成版本变化。
TEST_F(ImportBindingRepositoryTest, RangeSplitStillReportsStaleTokenSeparately) {
    make_defects_a_range();
    bridge_report::db::ComponentRangeSplitRepository repository(client_);
    const std::vector<bridge_report::review::ComponentRangeSplitTarget> targets{
        {"上部承重构件", "1-1#梁~1-25#梁"}};

    const auto applied = repository.apply(
        import_id_, targets, "sha256:deadbeef", user_id_, revision_id_);
    EXPECT_EQ(applied.status, bridge_report::db::ComponentRangeSplitStatus::Stale);
    EXPECT_EQ(applied.error_code, "component_range_split_stale");
}

// 拆分应用是写操作，要跟其他写路径一样把版本锁进年度。
TEST_F(ImportBindingRepositoryTest, RangeSplitApplyLocksTheYearRevision) {
    make_defects_a_range();
    bridge_report::db::ComponentRangeSplitRepository repository(client_);
    const std::vector<bridge_report::review::ComponentRangeSplitTarget> targets{
        {"上部承重构件", "1-1#梁~1-25#梁"}};
    const auto preview = repository.preview(import_id_, targets, revision_id_);
    ASSERT_EQ(preview.status, bridge_report::db::ComponentRangeSplitStatus::Ok);
    ASSERT_EQ(repository.apply(import_id_, targets, preview.impact_token, user_id_,
                               revision_id_).status,
              bridge_report::db::ComponentRangeSplitStatus::Ok);

    const auto year = client_->execSqlSync(
        "select component_inventory_revision_id::text as revision_id "
        "from inspection_years where id=$1::uuid", year_id_);
    ASSERT_FALSE(year[0]["revision_id"].isNull());
    EXPECT_EQ(year[0]["revision_id"].as<std::string>(), revision_id_);
}

// 同一个草稿优先排序在范围拆分里后果更硬：预览和应用都直接 Conflict，功能整个不可用。
TEST_F(ImportBindingRepositoryTest, RangeSplitStaysUsableWhileADraftExists) {
    add_revision(2, /*confirmed=*/false);
    make_defects_a_range();

    bridge_report::db::ComponentRangeSplitRepository repository(client_);
    const std::vector<bridge_report::review::ComponentRangeSplitTarget> targets{
        {"上部承重构件", "1-1#梁~1-25#梁"}};
    const auto preview = repository.preview(import_id_, targets, revision_id_);
    ASSERT_EQ(preview.status, bridge_report::db::ComponentRangeSplitStatus::Ok);
    ASSERT_TRUE(preview.analysis.has_value());
    EXPECT_EQ(preview.analysis->totals.bound_count, 3);

    const auto applied =
        repository.apply(import_id_, targets, preview.impact_token, user_id_, revision_id_);
    EXPECT_EQ(applied.status, bridge_report::db::ComponentRangeSplitStatus::Ok);
}

// 范围拆分此前完全不读 inspection_years，径直取桥梁最新版本，于是可能和绑定校验用的
// 不是同一份台账。这里锁定 R1、另建更新的已确认 R2（只含 9-9#梁）：取错版本就找不到
// 1-1#梁，bound_count 会掉到 0。
TEST_F(ImportBindingRepositoryTest, RangeSplitUsesTheRevisionLockedByTheYear) {
    client_->execSqlSync(
        "update inspection_years set component_inventory_revision_id=$2::uuid "
        "where id=$1::uuid",
        year_id_, revision_id_);
    add_revision(2, /*confirmed=*/true);
    make_defects_a_range();

    bridge_report::db::ComponentRangeSplitRepository repository(client_);
    const std::vector<bridge_report::review::ComponentRangeSplitTarget> targets{
        {"上部承重构件", "1-1#梁~1-25#梁"}};
    const auto preview = repository.preview(import_id_, targets, revision_id_);
    ASSERT_EQ(preview.status, bridge_report::db::ComponentRangeSplitStatus::Ok);
    ASSERT_TRUE(preview.analysis.has_value());
    EXPECT_EQ(preview.analysis->totals.bound_count, 3);
}

TEST_F(ImportBindingRepositoryTest, BindAttachesAllReferencingDefects) {
    ImportBindingRepository repository(client_);
    const auto outcome = repository.bind(import_id_, "上部承重构件", "1-1#梁", component_id_, revision_id_);
    ASSERT_EQ(outcome.status, BindingStatus::Ok) << static_cast<int>(outcome.status);
    const auto* row = find_row(*outcome.overview, "上部承重构件", "1-1#梁");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->status, "bound");
    ASSERT_TRUE(row->bridge_component_id.has_value());
    EXPECT_EQ(*row->bridge_component_id, component_id_);
    EXPECT_EQ(row->defect_count, 3);
    const auto stored = client_->execSqlSync(
        "select ir.parsed_result_json::text as parsed,"
        "iy.component_inventory_revision_id::text as year_revision_id "
        "from import_records ir "
        "join inspection_years iy on iy.id=ir.inspection_year_id "
        "where ir.id=$1::uuid",
        import_id_);
    EXPECT_EQ(stored[0]["year_revision_id"].as<std::string>(), revision_id_);
    Json::Value parsed;
    Json::CharReaderBuilder reader_builder;
    std::string errors;
    const auto text = stored[0]["parsed"].as<std::string>();
    const std::unique_ptr<Json::CharReader> reader(reader_builder.newCharReader());
    ASSERT_TRUE(reader->parse(text.data(), text.data() + text.size(), &parsed, &errors));
    ASSERT_EQ(parsed["defects"][0]["warnings"].size(), 1u);
    EXPECT_EQ(
        parsed["defects"][0]["warnings"][0]["code"].asString(),
        "defect_scale_invalid");
    EXPECT_TRUE(parsed["defects"][1]["warnings"].empty());
}

TEST_F(ImportBindingRepositoryTest, BindRejectsCategoryMismatch) {
    ImportBindingRepository repository(client_);
    // 把支座行绑到上部承重构件构件 → 类别不符。
    const auto outcome = repository.bind(import_id_, "支座", "2-1#支座", component_id_, revision_id_);
    EXPECT_EQ(outcome.status, BindingStatus::Conflict);
}

TEST_F(ImportBindingRepositoryTest, MarkMissingAndClearRoundTrip) {
    ImportBindingRepository repository(client_);
    const auto missing = repository.mark_missing(import_id_, "支座", "2-1#支座", revision_id_);
    ASSERT_EQ(missing.status, BindingStatus::Ok);
    EXPECT_EQ(find_row(*missing.overview, "支座", "2-1#支座")->status, "missing");
    const auto after_missing = client_->execSqlSync(
        "select jsonb_array_length(ir.parsed_result_json#>'{defects,3,warnings}') as count,"
        "iy.component_inventory_revision_id::text as year_revision_id "
        "from import_records ir "
        "join inspection_years iy on iy.id=ir.inspection_year_id "
        "where ir.id=$1::uuid", import_id_);
    EXPECT_EQ(after_missing[0]["count"].as<int>(), 0);
    EXPECT_EQ(after_missing[0]["year_revision_id"].as<std::string>(), revision_id_);

    const auto bound = repository.bind(import_id_, "上部承重构件", "1-1#梁", component_id_, revision_id_);
    ASSERT_EQ(bound.status, BindingStatus::Ok);
    const auto cleared = repository.clear(import_id_, "上部承重构件", "1-1#梁", revision_id_);
    ASSERT_EQ(cleared.status, BindingStatus::Ok);
    EXPECT_EQ(find_row(*cleared.overview, "上部承重构件", "1-1#梁")->status, "unmatched");
    const auto stored = client_->execSqlSync(
        "select parsed_result_json#>>'{defects,0,warnings,1,code}' as warning_code "
        "from import_records where id=$1::uuid", import_id_);
    EXPECT_EQ(
        stored[0]["warning_code"].as<std::string>(),
        "defect_component_match_required");
}

TEST_F(ImportBindingRepositoryTest, BindBatchAppliesEveryTarget) {
    ImportBindingRepository repository(client_);
    const auto outcome = repository.bind_batch(import_id_, {
        {"上部承重构件", "1-1#梁", component_id_},
    }, revision_id_);
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
    }, revision_id_);
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
    }, revision_id_);
    EXPECT_EQ(outcome.status, BindingStatus::Invalid);
    EXPECT_EQ(outcome.rejected_component_number, "9-9#不存在");
}

TEST_F(ImportBindingRepositoryTest, BindBatchRejectsEmptyTargets) {
    ImportBindingRepository repository(client_);
    EXPECT_EQ(repository.bind_batch(import_id_, {}, revision_id_).status, BindingStatus::Invalid);
}

TEST_F(ImportBindingRepositoryTest, BindsPublishedRatingTreeAndDerivesCompatibleInventory) {
    const auto trees = client_->execSqlSync(
        "select id::text as id from rating_tree_versions "
        "where status='published' order by published_at desc,id limit 1");
    if (trees.empty()) {
        GTEST_SKIP() << "No published rating tree synchronized";
    }
    const auto tree_id = trees[0]["id"].as<std::string>();

    ImportBindingRepository repository(client_);
    const auto outcome =
        repository.bind_rating_tree(import_id_, tree_id, user_id_, revision_id_);
    ASSERT_EQ(outcome.status, BindingStatus::Ok)
        << static_cast<int>(outcome.status);
    ASSERT_TRUE(outcome.overview.has_value());
    ASSERT_TRUE(outcome.overview->rating_tree.has_value());
    EXPECT_EQ(outcome.overview->rating_tree->version_id, tree_id);

    const auto year = client_->execSqlSync(
        "select profile.rating_tree_version_id::text as tree_id,"
        "iy.component_inventory_revision_id::text as revision_id,"
        "revision.status as revision_status,"
        "revision.baseline_revision_id::text as baseline_id "
        "from inspection_years iy "
        "join project_standard_profiles profile "
        "on profile.id=iy.standard_profile_id "
        "join bridge_component_inventory_revisions revision "
        "on revision.id=iy.component_inventory_revision_id "
        "where iy.id=$1::uuid",
        year_id_);
    ASSERT_EQ(year.size(), 1u);
    EXPECT_EQ(year[0]["tree_id"].as<std::string>(), tree_id);
    EXPECT_EQ(year[0]["revision_status"].as<std::string>(), "已确认");
    EXPECT_EQ(year[0]["baseline_id"].as<std::string>(), revision_id_);
    EXPECT_NE(year[0]["revision_id"].as<std::string>(), revision_id_);
}

TEST_F(ImportBindingRepositoryTest, RejectsWritesOutsidePendingReview) {
    client_->execSqlSync("update import_records set import_status='已确认' where id=$1::uuid", import_id_);
    ImportBindingRepository repository(client_);
    EXPECT_EQ(repository.overview(import_id_).status, BindingStatus::Conflict);
    EXPECT_EQ(repository.bind(import_id_, "上部承重构件", "1-1#梁", component_id_, revision_id_).status,
              BindingStatus::Conflict);
    EXPECT_EQ(repository.mark_missing(import_id_, "支座", "2-1#支座", revision_id_).status, BindingStatus::Conflict);
    EXPECT_EQ(repository.bind_batch(import_id_, {{"上部承重构件", "1-1#梁", component_id_}}, revision_id_).status,
              BindingStatus::Conflict);
    EXPECT_EQ(
        repository.bind_rating_tree(
            import_id_, "11111111-1111-1111-1111-111111111111",
            user_id_, revision_id_).status,
        BindingStatus::Conflict);
}

}  // namespace

// 独占编辑是后端边界，不能只靠前端隐藏按钮。这六个写接口改的是
// import_records.parsed_result_json——和校对草稿保存写的同一份数据。不校验锁的话，
// 另一个已登录用户可以在别人持锁时改它，而持锁者随后的整份草稿保存又会把这些
// 修改静默覆盖掉。
TEST_F(ImportBindingRepositoryTest, WriteEndpointsRejectAnInvalidEditLock) {
    bridge_report::db::ImportBindingRepository repository(client_);
    // 从未签发过的令牌：代表另一个用户/会话，或锁已过期、已被管理员强制收回。
    const bridge_report::db::EditLockCredentials foreign{
        user_id_, user_id_, "never-issued-token"};

    EXPECT_EQ(repository.bind(import_id_, "上部承重构件", "1-1#梁", component_id_,
                              revision_id_, foreign).status,
              bridge_report::db::BindingStatus::EditLockInvalid);
    EXPECT_EQ(repository.bind_batch(
                  import_id_, {{"上部承重构件", "1-1#梁", component_id_}},
                  revision_id_, foreign).status,
              bridge_report::db::BindingStatus::EditLockInvalid);
    EXPECT_EQ(repository.mark_missing(import_id_, "上部承重构件", "1-1#梁",
                                      revision_id_, foreign).status,
              bridge_report::db::BindingStatus::EditLockInvalid);
    EXPECT_EQ(repository.clear(import_id_, "上部承重构件", "1-1#梁",
                               revision_id_, foreign).status,
              bridge_report::db::BindingStatus::EditLockInvalid);

    // 一条都不许写进去。
    const auto stored = client_->execSqlSync(
        "select parsed_result_json::text as parsed from import_records where id=$1::uuid",
        import_id_)[0]["parsed"].as<std::string>();
    EXPECT_EQ(stored.find(component_id_), std::string::npos)
        << "编辑锁校验失败时不得写入任何绑定";
}

// 持有有效锁时照常放行——加的是边界，不是把功能关掉。
TEST_F(ImportBindingRepositoryTest, WriteEndpointsAcceptTheActiveEditLock) {
    // 编辑锁行外键指向 user_sessions，得先有一个真实会话。
    const auto session_id = client_->execSqlSync(
        "insert into user_sessions(user_id,token_hash,expires_at) "
        "values($1::uuid,'binding-lock-session',now()+interval '1 hour') returning id::text",
        user_id_)[0]["id"].as<std::string>();
    bridge_report::db::EditLockRepository locks(client_);
    const auto owner = bridge_report::db::AuthUser{
        user_id_, session_id, "admin", "管理员", "admin"};
    const auto acquired = locks.acquire(import_id_, owner);
    ASSERT_TRUE(acquired.acquired);

    bridge_report::db::ImportBindingRepository repository(client_);
    const bridge_report::db::EditLockCredentials held{
        user_id_, session_id, acquired.lock_token};
    const auto outcome = repository.bind(
        import_id_, "上部承重构件", "1-1#梁", component_id_, revision_id_, held);

    EXPECT_EQ(outcome.status, bridge_report::db::BindingStatus::Ok)
        << "持锁用户必须能正常绑定";
    client_->execSqlSync(
        "delete from import_record_edit_locks where import_record_id=$1::uuid", import_id_);
    client_->execSqlSync("delete from user_sessions where id=$1::uuid", session_id);
}

// 不传凭证时跳过校验：测试夹具与既有调用点靠这条保持可用，生产路由一律传。
TEST_F(ImportBindingRepositoryTest, OmittingTheCredentialsSkipsTheCheck) {
    bridge_report::db::ImportBindingRepository repository(client_);
    EXPECT_EQ(repository.bind(import_id_, "上部承重构件", "1-1#梁", component_id_,
                              revision_id_).status,
              bridge_report::db::BindingStatus::Ok);
}

// 年度切到新版本后，按旧版本提交的写操作必须被 expected_inventory_revision_id 挡下，
// 按新版本提交则放行且病害带上新版本。
//
// 注意这条**不**覆盖年度行锁：把 for update 去掉它照样绿——没有交错时，联查读到的和
// 行锁内读到的是同一个值。行锁由下面那条 WriteBlocksWhileAnotherTransactionHoldsTheYearRow 钉。
TEST_F(ImportBindingRepositoryTest, WritesUseTheRevisionCurrentlyLockedOnTheYear) {
    // 桥上再确认一个版本，并把年度切过去——模拟另一条共享该年度的导入记录刚重绑评定树。
    const auto newer_revision = client_->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
        "baseline_revision_id,created_by_user_id) values($1::uuid,2,$2::uuid,$3::uuid) "
        "returning id::text",
        bridge_id_, revision_id_, user_id_)[0]["id"].as<std::string>();
    client_->execSqlSync(
        "insert into bridge_component_inventory_entries(inventory_revision_id,"
        "bridge_component_id,component_number,site_name,site_component_type,sort_order) "
        "select $1::uuid,bridge_component_id,component_number,site_name,site_component_type,"
        "sort_order from bridge_component_inventory_entries where inventory_revision_id=$2::uuid",
        newer_revision, revision_id_);
    client_->execSqlSync(
        "insert into bridge_component_standard_mappings(inventory_entry_id,standard_package_id,"
        "standard_bridge_type_id,standard_component_category_id,structure_part,mapping_source,"
        "confirmation_status,confirmed_by_user_id,confirmed_at) "
        "select e.id,m.standard_package_id,m.standard_bridge_type_id,"
        "m.standard_component_category_id,m.structure_part,m.mapping_source,"
        "m.confirmation_status,m.confirmed_by_user_id,m.confirmed_at "
        "from bridge_component_standard_mappings m "
        "join bridge_component_inventory_entries o on o.id=m.inventory_entry_id "
        "  and o.inventory_revision_id=$2::uuid "
        "join bridge_component_inventory_entries e on e.inventory_revision_id=$1::uuid "
        "  and e.bridge_component_id=o.bridge_component_id",
        newer_revision, revision_id_);
    client_->execSqlSync(
        "update bridge_component_inventory_revisions set status='已确认',"
        "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
        newer_revision, user_id_);
    client_->execSqlSync(
        "update inspection_years set component_inventory_revision_id=$2::uuid where id=$1::uuid",
        year_id_, newer_revision);

    bridge_report::db::ImportBindingRepository repository(client_);

    // 按旧版本提交：写事务在年度行锁内解析出的是 newer_revision，与期望不符 -> 拒绝。
    const auto stale = repository.bind(
        import_id_, "上部承重构件", "1-1#梁", component_id_, revision_id_);
    EXPECT_EQ(stale.status, bridge_report::db::BindingStatus::Conflict);
    EXPECT_EQ(stale.error_code, "component_inventory_revision_changed");

    // 按年度当前锁定的版本提交：放行，并且病害带的就是这个版本。
    const auto fresh = repository.bind(
        import_id_, "上部承重构件", "1-1#梁", component_id_, newer_revision);
    ASSERT_EQ(fresh.status, bridge_report::db::BindingStatus::Ok)
        << fresh.error_code << ": " << fresh.error_message;
    const auto stored = client_->execSqlSync(
        "select parsed_result_json#>>'{defects,0,component_inventory_revision_id}' as revision_id "
        "from import_records where id=$1::uuid",
        import_id_)[0]["revision_id"].as<std::string>();
    EXPECT_EQ(stored, newer_revision);
}

// 写路径必须在**年度行锁内**读锁定的台账版本。这条直接钉行锁本身：
// 另一条连接握着该年度行的 FOR UPDATE 时，写事务必须被挡在那里。
//
// 判据靠 statement_timeout 做成确定的，不依赖时序运气：
//   - 有行锁：bind 卡在 select ... for update 上 -> 语句超时 -> Failed；
//   - 无行锁：联查不加锁，年度已锁定版本时 lock_pending_year_revision 又直接短路
//     不写库，于是整条 bind 一路通到底 -> Ok。
// 两个结果泾渭分明，把行锁去掉这条必红。
TEST_F(ImportBindingRepositoryTest, WriteBlocksWhileAnotherTransactionHoldsTheYearRow) {
    // 年度先锁定到夹具那个已确认版本，让 lock_pending_year_revision 走"已锁定"短路，
    // 排除掉"其实是被那句 UPDATE 挡住的"这种解释。
    client_->execSqlSync(
        "update inspection_years set component_inventory_revision_id=$2::uuid where id=$1::uuid",
        year_id_, revision_id_);

    // 写仓储用独占一条连接的客户端：连接池只有 1 条时 SET 才会落在后续事务用的
    // 那条连接上。
    auto writer_client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);
    writer_client->execSqlSync("set statement_timeout = '1500'");

    auto blocker = client_->newTransaction();
    blocker->execSqlSync(
        "select id from inspection_years where id=$1::uuid for update", year_id_);

    const auto outcome = bridge_report::db::ImportBindingRepository(writer_client).bind(
        import_id_, "上部承重构件", "1-1#梁", component_id_, revision_id_);

    blocker->rollback();
    writer_client->closeAll();

    EXPECT_EQ(outcome.status, bridge_report::db::BindingStatus::Failed)
        << "别人握着年度行锁时，写事务必须被挡住而不是照常写下去";
    const auto stored = client_->execSqlSync(
        "select parsed_result_json::text as parsed from import_records where id=$1::uuid",
        import_id_)[0]["parsed"].as<std::string>();
    EXPECT_EQ(stored.find(component_id_), std::string::npos) << "被挡住时不得有任何写入";
}
