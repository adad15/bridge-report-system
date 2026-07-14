#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <drogon/orm/DbClient.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ReviewRepository.hpp"
#include "bridge_report/review/ConfirmPlan.hpp"
#include "bridge_report/review/PreflightReport.hpp"
#include "bridge_report/review/ReviewModels.hpp"
#include "support/review_fixtures.hpp"

namespace {

using bridge_report::test_support::confirm_all_candidates;

std::string read_fixture_text(const std::string& file_name) {
    const auto path = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) / "samples" / "contracts" / file_name;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open fixture: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

Json::Value parse_json_text(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        throw std::runtime_error("Unable to parse JSON text: " + errors);
    }
    return root;
}

std::string write_json_compact(const Json::Value& value) {
    Json::StreamWriterBuilder writer_builder;
    writer_builder["indentation"] = "";
    return Json::writeString(writer_builder, value);
}

// 组装 PreflightContext 并调用 build_preflight_report 的完整流程，
// 与 POST preflight-confirm 路由的组装逻辑一致（用于集成测试验证端到端行为）。
bridge_report::review::PreflightReport run_preflight_flow(
    bridge_report::db::ReviewRepository& repository,
    const std::string& import_record_id
) {
    const auto detail = repository.get_import_record_detail(import_record_id);
    if (!detail.has_value()) {
        throw std::runtime_error("import record not found: " + import_record_id);
    }

    const auto parsed_result = parse_json_text(detail->parsed_result_json);
    const auto effective_year = bridge_report::review::resolve_effective_inspection_year(*detail, parsed_result);
    const bool has_current_annual_facts =
        effective_year.has_value() && repository.has_current_annual_facts(detail->bridge_id, *effective_year);

    const auto context =
        bridge_report::review::build_preflight_context(*detail, effective_year, has_current_annual_facts);
    return bridge_report::review::build_preflight_report(parsed_result, context);
}

// fixture：在事务里插入一座桥 + 一个年度 + 一条导入记录，测试结束后回滚，数据库保持不变。
class ReviewRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* env_value = std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL");
        if (env_value == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }

        const bridge_report::config::PostgresConfig config{};
        client_ = bridge_report::db::create_db_client(config, 1);
        tx_ = client_->newTransaction();

        const auto bridge_result = tx_->execSqlSync(
            "insert into bridges (bridge_name, route_name, status) "
            "values ($1, $2, $3) returning id, system_number",
            "M05T2测试桥梁",
            "G1线",
            "在用"
        );
        bridge_id_ = bridge_result[0]["id"].as<std::string>();
        bridge_system_number_ = bridge_result[0]["system_number"].as<std::string>();

        const auto year_result = tx_->execSqlSync(
            "insert into inspection_years "
            "(bridge_id, inspection_year, status, version_number, is_current) "
            "values ($1::uuid, $2, $3, $4, $5) returning id",
            bridge_id_,
            2025,
            "已确认",
            1,
            true
        );
        inspection_year_id_ = year_result[0]["id"].as<std::string>();

        const auto import_result = tx_->execSqlSync(
            "insert into import_records "
            "(bridge_id, inspection_year_id, import_name, source_type, import_status, importer_name) "
            "values ($1::uuid, $2::uuid, $3, $4, $5, $6) returning id",
            bridge_id_,
            inspection_year_id_,
            "M05T2测试导入.docx",
            "正式Word",
            "待校对",
            "张三"
        );
        import_record_id_ = import_result[0]["id"].as<std::string>();
    }

    void TearDown() override {
        if (tx_ != nullptr) {
            tx_->rollback();
        }
    }

    drogon::orm::DbClientPtr client_;
    std::shared_ptr<drogon::orm::Transaction> tx_;
    std::string bridge_id_;
    std::string bridge_system_number_;
    std::string inspection_year_id_;
    std::string import_record_id_;
};

}  // 匿名命名空间

TEST_F(ReviewRepositoryTest, list_bridges_returns_fixture_bridge) {
    bridge_report::db::ReviewRepository repository(tx_);

    const auto bridges = repository.list_bridges();

    bool found = false;
    for (const auto& bridge : bridges) {
        if (bridge.id == bridge_id_) {
            found = true;
            EXPECT_EQ(bridge.system_number, bridge_system_number_);
            EXPECT_EQ(bridge.bridge_name, "M05T2测试桥梁");
            ASSERT_TRUE(bridge.route_name.has_value());
            EXPECT_EQ(*bridge.route_name, "G1线");
            EXPECT_EQ(bridge.status, "在用");
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ReviewRepositoryTest, list_inspection_years_returns_fixture_year) {
    bridge_report::db::ReviewRepository repository(tx_);

    const auto years = repository.list_inspection_years(bridge_id_);

    ASSERT_EQ(years.size(), 1u);
    EXPECT_EQ(years[0].id, inspection_year_id_);
    EXPECT_EQ(years[0].inspection_year, 2025);
    EXPECT_EQ(years[0].status, "已确认");
    EXPECT_EQ(years[0].version_number, 1);
    EXPECT_TRUE(years[0].is_current);
}

TEST_F(ReviewRepositoryTest, list_import_records_returns_fixture_record) {
    bridge_report::db::ReviewRepository repository(tx_);

    const auto records = repository.list_import_records(bridge_id_);

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].id, import_record_id_);
    EXPECT_EQ(records[0].import_name, "M05T2测试导入.docx");
    EXPECT_EQ(records[0].source_type, "正式Word");
    EXPECT_EQ(records[0].import_status, "待校对");
    ASSERT_TRUE(records[0].inspection_year_id.has_value());
    EXPECT_EQ(*records[0].inspection_year_id, inspection_year_id_);
    ASSERT_TRUE(records[0].importer_name.has_value());
    EXPECT_EQ(*records[0].importer_name, "张三");
}

TEST_F(ReviewRepositoryTest, ResolvesPhotoOnlyThroughCurrentImportFileLinks) {
    const auto archived = tx_->execSqlSync(
        "insert into archived_files (bridge_id, inspection_year_id, original_file_name, current_file_name, "
        "storage_relative_path, file_type, file_purpose, file_extension) "
        "values ($1::uuid, $2::uuid, 'photo.jpg', 'photo.jpg', "
        "'bridges/sample/photos/photo_0001.jpg', '图片', 'Word病害照片', 'jpg') returning id",
        bridge_id_, inspection_year_id_);
    const auto archived_id = archived[0]["id"].as<std::string>();
    tx_->execSqlSync(
        "insert into import_record_files (import_record_id, archived_file_id, file_role, process_status) "
        "values ($1::uuid, $2::uuid, '附件', '处理成功')", import_record_id_, archived_id);
    Json::Value data;
    data["photos"] = Json::Value(Json::arrayValue);
    Json::Value photo;
    photo["candidate_id"] = "photo_0001";
    photo["extracted_file"]["archive_relative_path"] = "bridges/sample/photos/photo_0001.jpg";
    data["photos"].append(photo);
    tx_->execSqlSync(
        "update import_records set parsed_result_json = $2::jsonb where id = $1::uuid",
        import_record_id_, write_json_compact(data));

    bridge_report::db::ReviewRepository repository(tx_);
    const auto ref = repository.get_photo_content_ref(import_record_id_, "photo_0001");

    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->archived_file_id, archived_id);
    EXPECT_EQ(ref->storage_relative_path, "bridges/sample/photos/photo_0001.jpg");
    EXPECT_EQ(ref->content_type, "image/jpeg");
    EXPECT_FALSE(repository.get_photo_content_ref(import_record_id_, "photo_missing").has_value());
}

TEST_F(ReviewRepositoryTest, list_inspection_years_returns_empty_for_unknown_bridge) {
    bridge_report::db::ReviewRepository repository(tx_);

    const auto years = repository.list_inspection_years("00000000-0000-0000-0000-000000000000");

    EXPECT_TRUE(years.empty());
}

TEST_F(ReviewRepositoryTest, get_import_record_detail_returns_bridge_year_and_parsed_result) {
    const auto fixture_json = read_fixture_text("bridge_annual_inspection_data.valid.json");
    tx_->execSqlSync(
        "update import_records set parsed_result_json = $1::jsonb where id = $2::uuid",
        fixture_json,
        import_record_id_
    );

    bridge_report::db::ReviewRepository repository(tx_);

    const auto detail = repository.get_import_record_detail(import_record_id_);

    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->id, import_record_id_);
    EXPECT_EQ(detail->bridge_id, bridge_id_);
    EXPECT_EQ(detail->bridge_name, "M05T2测试桥梁");
    ASSERT_TRUE(detail->inspection_year_id.has_value());
    EXPECT_EQ(*detail->inspection_year_id, inspection_year_id_);
    ASSERT_TRUE(detail->inspection_year.has_value());
    EXPECT_EQ(*detail->inspection_year, 2025);
    ASSERT_TRUE(detail->inspection_year_status.has_value());
    EXPECT_EQ(*detail->inspection_year_status, "已确认");

    Json::CharReaderBuilder builder;
    Json::Value parsed;
    std::string errors;
    std::istringstream stream(detail->parsed_result_json);
    ASSERT_TRUE(Json::parseFromStream(builder, stream, &parsed, &errors)) << errors;
    ASSERT_TRUE(parsed.isMember("defects"));
    EXPECT_EQ(parsed["defects"].size(), 1u);
}

TEST_F(ReviewRepositoryTest, get_import_record_detail_returns_null_inspection_year_when_absent) {
    const auto import_result = tx_->execSqlSync(
        "insert into import_records "
        "(bridge_id, import_name, source_type, import_status) "
        "values ($1::uuid, $2, $3, $4) returning id",
        bridge_id_,
        "M05T2无年度导入.docx",
        "正式Word",
        "已上传"
    );
    const auto import_record_id_without_year = import_result[0]["id"].as<std::string>();

    bridge_report::db::ReviewRepository repository(tx_);

    const auto detail = repository.get_import_record_detail(import_record_id_without_year);

    ASSERT_TRUE(detail.has_value());
    EXPECT_FALSE(detail->inspection_year_id.has_value());
    EXPECT_FALSE(detail->inspection_year.has_value());
    EXPECT_FALSE(detail->inspection_year_status.has_value());
    EXPECT_FALSE(detail->inspection_year_version_number.has_value());
    EXPECT_FALSE(detail->inspection_year_is_current.has_value());
}

TEST_F(ReviewRepositoryTest, get_import_record_detail_returns_nullopt_for_unknown_id) {
    bridge_report::db::ReviewRepository repository(tx_);

    const auto detail = repository.get_import_record_detail("00000000-0000-0000-0000-000000000000");

    EXPECT_FALSE(detail.has_value());
}

TEST_F(ReviewRepositoryTest, has_current_annual_facts_reflects_inspection_year_state) {
    bridge_report::db::ReviewRepository repository(tx_);

    // fixture 中已经插入了一条“已确认 + is_current”的 2025 年度记录。
    EXPECT_TRUE(repository.has_current_annual_facts(bridge_id_, 2025));

    // 该桥梁没有 2030 年度的记录。
    EXPECT_FALSE(repository.has_current_annual_facts(bridge_id_, 2030));
}

TEST_F(ReviewRepositoryTest, save_review_draft_updates_parsed_result_and_keeps_status) {
    const auto fixture_json = read_fixture_text("bridge_annual_inspection_data.valid.json");

    bridge_report::db::ReviewRepository repository(tx_);
    const bool saved = repository.save_review_draft(import_record_id_, fixture_json);

    EXPECT_TRUE(saved);
    const auto detail = repository.get_import_record_detail(import_record_id_);
    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->import_status, "待校对");

    Json::CharReaderBuilder builder;
    Json::Value parsed;
    std::string errors;
    std::istringstream stream(detail->parsed_result_json);
    ASSERT_TRUE(Json::parseFromStream(builder, stream, &parsed, &errors)) << errors;
    ASSERT_TRUE(parsed.isMember("defects"));
    EXPECT_EQ(parsed["defects"].size(), 1u);
}

TEST_F(ReviewRepositoryTest, save_review_draft_returns_false_and_leaves_json_when_not_pending_review) {
    tx_->execSqlSync(
        "update import_records set import_status = $1 where id = $2::uuid",
        "已取消",
        import_record_id_
    );
    const auto fixture_json = read_fixture_text("bridge_annual_inspection_data.valid.json");

    bridge_report::db::ReviewRepository repository(tx_);
    const bool saved = repository.save_review_draft(import_record_id_, fixture_json);

    EXPECT_FALSE(saved);
    const auto detail = repository.get_import_record_detail(import_record_id_);
    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->import_status, "已取消");
    // fixture 插入时未写 parsed_result_json，应保持建表默认值 {}，未被草稿覆盖。
    EXPECT_EQ(detail->parsed_result_json, "{}");
}

TEST_F(ReviewRepositoryTest, cancel_import_record_transitions_pending_review_to_cancelled) {
    bridge_report::db::ReviewRepository repository(tx_);

    // fixture 中的导入记录状态为“待校对”。
    const bool cancelled = repository.cancel_import_record(import_record_id_);

    EXPECT_TRUE(cancelled);
    const auto detail = repository.get_import_record_detail(import_record_id_);
    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->import_status, "已取消");
}

TEST_F(ReviewRepositoryTest, cancel_import_record_returns_false_when_already_confirmed) {
    tx_->execSqlSync(
        "update import_records set import_status = $1 where id = $2::uuid",
        "已确认",
        import_record_id_
    );

    bridge_report::db::ReviewRepository repository(tx_);
    const bool cancelled = repository.cancel_import_record(import_record_id_);

    EXPECT_FALSE(cancelled);
    const auto detail = repository.get_import_record_detail(import_record_id_);
    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->import_status, "已确认");
}

TEST_F(ReviewRepositoryTest, cancel_import_record_returns_false_when_already_cancelled) {
    tx_->execSqlSync(
        "update import_records set import_status = $1 where id = $2::uuid",
        "已取消",
        import_record_id_
    );

    bridge_report::db::ReviewRepository repository(tx_);
    const bool cancelled = repository.cancel_import_record(import_record_id_);

    EXPECT_FALSE(cancelled);
    const auto detail = repository.get_import_record_detail(import_record_id_);
    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->import_status, "已取消");
}

TEST_F(ReviewRepositoryTest, has_current_annual_facts_false_before_confirmed_year_inserted) {
    const auto bridge_result = tx_->execSqlSync(
        "insert into bridges (bridge_name, route_name, status) "
        "values ($1, $2, $3) returning id",
        "M05T2第二测试桥梁",
        "G2线",
        "在用"
    );
    const auto second_bridge_id = bridge_result[0]["id"].as<std::string>();

    bridge_report::db::ReviewRepository repository(tx_);

    EXPECT_FALSE(repository.has_current_annual_facts(second_bridge_id, 2026));

    tx_->execSqlSync(
        "insert into inspection_years "
        "(bridge_id, inspection_year, status, version_number, is_current) "
        "values ($1::uuid, $2, $3, $4, $5)",
        second_bridge_id,
        2026,
        "已确认",
        1,
        true
    );

    EXPECT_TRUE(repository.has_current_annual_facts(second_bridge_id, 2026));
}

TEST_F(ReviewRepositoryTest, preflight_flow_blocks_on_pending_candidates_then_confirms_after_review) {
    bridge_report::db::ReviewRepository repository(tx_);

    // 先取一次详情，拿到本次测试实际生成的编号（system_number 由数据库序列生成，不可硬编码），
    // 用于把样例 JSON 的 import_context / bridge_check / inspection_year 对齐到本条记录，
    // 避免 import_context_mismatch 掩盖本测试关注的 candidate_pending_review 场景。
    const auto initial_detail = repository.get_import_record_detail(import_record_id_);
    ASSERT_TRUE(initial_detail.has_value());

    auto data = parse_json_text(read_fixture_text("bridge_annual_inspection_data.valid.json"));
    data["import_context"]["import_record_system_number"] = initial_detail->system_number;
    data["bridge_check"]["selected_bridge_system_number"] = initial_detail->bridge_system_number;
    ASSERT_TRUE(initial_detail->inspection_year.has_value());
    data["inspection"]["inspection_year"] = *initial_detail->inspection_year;

    // 样例默认所有候选都是“待确认”，直接保存即可满足“有待确认候选”场景。
    ASSERT_TRUE(repository.save_review_draft(import_record_id_, write_json_compact(data)));

    const auto pending_report = run_preflight_flow(repository, import_record_id_);

    EXPECT_FALSE(pending_report.can_confirm);
    bool has_pending_candidate_issue = false;
    for (const auto& issue : pending_report.blocking_errors) {
        if (issue.code == "candidate_pending_review") {
            has_pending_candidate_issue = true;
            break;
        }
    }
    EXPECT_TRUE(has_pending_candidate_issue);

    // 把三层候选全部改为“已确认”后重新保存，再走一遍流程应当放行。
    confirm_all_candidates(data);
    ASSERT_TRUE(repository.save_review_draft(import_record_id_, write_json_compact(data)));

    const auto confirmed_report = run_preflight_flow(repository, import_record_id_);

    EXPECT_TRUE(confirmed_report.can_confirm);
    EXPECT_TRUE(confirmed_report.blocking_errors.empty());
}

// ---------------------------------------------------------------------------
// confirm_annual_facts：唯一的年度事实入库写入口。
//
// confirm_annual_facts 内部通过 db_client_->newTransaction() 开启自己的事务，
// 而 ReviewRepositoryTest 的 fixture 把所有插入都包在一个外层事务里、TearDown 时整体
// rollback——两者叠加会变成事务套事务（不受支持）。因此这里使用一个独立的 fixture，
// 直接用裸 client_ 执行插入（每条语句自成一个隐式事务，立即生效），TearDown 按依赖顺序
// 显式删除本测试插入的行。
// ---------------------------------------------------------------------------

namespace {

class ConfirmAnnualFactsTest : public ::testing::Test {
protected:
    static constexpr int kInspectionYear = 2025;

    void SetUp() override {
        const char* env_value = std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL");
        if (env_value == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }

        const bridge_report::config::PostgresConfig config{};
        client_ = bridge_report::db::create_db_client(config, 1);

        const auto bridge_result = client_->execSqlSync(
            "insert into bridges (bridge_name, route_name, status) "
            "values ($1, $2, $3) returning id, system_number",
            "M05T8测试桥梁",
            "G8线",
            "在用"
        );
        bridge_id_ = bridge_result[0]["id"].as<std::string>();
        bridge_system_number_ = bridge_result[0]["system_number"].as<std::string>();

        // 占位年度行：待校对 + is_current=false，与规格步骤 2 的初始状态一致；
        // is_current 必须显式置为 false（而非依赖列默认值 true），否则修订测试另外插入的
        // “已确认 + is_current” 行会与这一行同时违反 ux_inspection_years_current_bridge_year。
        const auto year_result = client_->execSqlSync(
            "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current) "
            "values ($1::uuid, $2, '待校对', 1, false) returning id",
            bridge_id_,
            kInspectionYear
        );
        placeholder_year_id_ = year_result[0]["id"].as<std::string>();
        tracked_year_ids_.push_back(placeholder_year_id_);

        const auto import_result = client_->execSqlSync(
            "insert into import_records "
            "(bridge_id, inspection_year_id, import_name, source_type, import_status, importer_name) "
            "values ($1::uuid, $2::uuid, $3, $4, $5, $6) returning id, system_number",
            bridge_id_,
            placeholder_year_id_,
            "M05T8测试导入.docx",
            "正式Word",
            "待校对",
            "李四"
        );
        import_record_id_ = import_result[0]["id"].as<std::string>();
        import_record_system_number_ = import_result[0]["system_number"].as<std::string>();

        const auto archived = client_->execSqlSync(
            "insert into archived_files (bridge_id, inspection_year_id, original_file_name, current_file_name, "
            "storage_relative_path, file_type, file_purpose, file_extension) "
            "values ($1::uuid, $2::uuid, 'photo.jpg', 'photo.jpg', 'photos/2.1-1.jpg', "
            "'图片', 'Word病害照片', '.jpg') returning id",
            bridge_id_, placeholder_year_id_);
        photo_archived_file_id_ = archived[0]["id"].as<std::string>();
        client_->execSqlSync(
            "insert into import_record_files (import_record_id, archived_file_id, file_role, process_status) "
            "values ($1::uuid, $2::uuid, '附件', '处理成功')", import_record_id_, photo_archived_file_id_);
        client_->execSqlSync(
            "update import_records set parsed_result_json = $2::jsonb where id = $1::uuid",
            import_record_id_, write_json_compact(build_confirmed_data()));
    }

    void TearDown() override {
        if (client_ == nullptr) {
            return;
        }
        if (!import_record_id_.empty()) {
            // defect_observations 级联删除 defect_measurements / defect_photos。
            client_->execSqlSync(
                "delete from defect_observations where source_import_record_id = $1::uuid", import_record_id_
            );
            client_->execSqlSync(
                "delete from condition_ratings where source_import_record_id = $1::uuid", import_record_id_
            );
            client_->execSqlSync("delete from import_records where id = $1::uuid", import_record_id_);
        }
        if (!photo_archived_file_id_.empty()) {
            client_->execSqlSync("delete from archived_files where id = $1::uuid", photo_archived_file_id_);
        }
        if (!bridge_id_.empty()) {
            // bridge_components 级联删除 component_aliases；须在 inspection_years 之前删除
            // （defect_observations 已先删，不再持有 bridge_component_id 外键）。
            client_->execSqlSync("delete from bridge_components where bridge_id = $1::uuid", bridge_id_);
        }
        // 按插入的逆序删除年度行：修订场景下新行的 revision_source_inspection_id 指向旧行
        // （on delete restrict），必须先删新行再删旧行；tracked_year_ids_ 始终按“先存在的
        // 行先入列表”的顺序追加，逆序遍历即满足这一依赖方向。
        for (auto it = tracked_year_ids_.rbegin(); it != tracked_year_ids_.rend(); ++it) {
            client_->execSqlSync("delete from inspection_years where id = $1::uuid", *it);
        }
        if (!bridge_id_.empty()) {
            client_->execSqlSync("delete from bridges where id = $1::uuid", bridge_id_);
        }
        // 显式关闭连接，而不是让 client_ 在夹具析构时才隐式释放——本 fixture（不同于
        // ReviewRepositoryTest）在裸 client_ 上做了大量各自独立提交的语句，外加
        // confirm_annual_facts 内部另开的事务，连接/事务生命周期的churn 明显更高；
        // 让析构前的关闭时机确定下来，避免进程退出阶段与 drogon 内部异步清理产生竞争。
        client_->closeAll();
    }

    // 组装与本 fixture 生成的桥梁/导入记录标识对齐、且三层候选全部“已确认”的样例数据。
    Json::Value build_confirmed_data() const {
        auto data = parse_json_text(read_fixture_text("bridge_annual_inspection_data.valid.json"));
        data["import_context"]["import_record_system_number"] = import_record_system_number_;
        data["bridge_check"]["selected_bridge_system_number"] = bridge_system_number_;
        data["inspection"]["inspection_year"] = kInspectionYear;
        confirm_all_candidates(data);
        return data;
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string bridge_system_number_;
    std::string import_record_id_;
    std::string import_record_system_number_;
    std::string placeholder_year_id_;
    std::string photo_archived_file_id_;
    std::vector<std::string> tracked_year_ids_;
};

}  // 匿名命名空间

TEST_F(ConfirmAnnualFactsTest, ReadsLatestJsonInsteadOfCallerSnapshot) {
    bridge_report::db::ReviewRepository repository(client_);
    auto latest = build_confirmed_data();
    latest["defects"][0]["component_name"] = "事务内最新构件";
    ASSERT_TRUE(repository.save_review_draft(import_record_id_, write_json_compact(latest)));

    const auto outcome = repository.confirm_annual_facts(import_record_id_, false, "确认最新草稿");

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    const auto rows = client_->execSqlSync(
        "select business_component_code from defect_observations where source_import_record_id = $1::uuid",
        import_record_id_);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0]["business_component_code"].as<std::string>(), "事务内最新构件");
}

TEST_F(ConfirmAnnualFactsTest, ReturnsTransactionTimePreflightDetailsForPendingLatestDraft) {
    bridge_report::db::ReviewRepository repository(client_);
    auto latest = build_confirmed_data();
    latest["defects"][0]["review_status"] = "待确认";
    ASSERT_TRUE(repository.save_review_draft(import_record_id_, write_json_compact(latest)));

    const auto outcome = repository.confirm_annual_facts(import_record_id_, false, "");

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "preflight_failed");
    EXPECT_FALSE(outcome.preflight_details["can_confirm"].asBool());
    EXPECT_FALSE(outcome.preflight_details["blocking_errors"].empty());
}

TEST_F(ConfirmAnnualFactsTest, MissingCurrentImportPhotoLinkBlocksAndRollsBack) {
    bridge_report::db::ReviewRepository repository(client_);
    client_->execSqlSync(
        "delete from import_record_files where import_record_id = $1::uuid and archived_file_id = $2::uuid",
        import_record_id_, photo_archived_file_id_);

    const auto outcome = repository.confirm_annual_facts(import_record_id_, false, "");

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "preflight_failed");
    ASSERT_EQ(outcome.preflight_details["blocking_errors"].size(), 1u);
    EXPECT_EQ(outcome.preflight_details["blocking_errors"][0]["code"].asString(), "photo_archive_missing");
    const auto facts = client_->execSqlSync(
        "select count(*) as n from defect_observations where source_import_record_id = $1::uuid", import_record_id_);
    EXPECT_EQ(facts[0]["n"].as<long long>(), 0);
}

TEST_F(ConfirmAnnualFactsTest, RejectsInspectionYearOwnedByAnotherBridge) {
    const auto other_bridge = client_->execSqlSync(
        "insert into bridges (bridge_name) values ('错误挂载目标桥') returning id");
    const auto other_bridge_id = other_bridge[0]["id"].as<std::string>();
    const auto other_year = client_->execSqlSync(
        "insert into inspection_years (bridge_id, inspection_year, status, is_current) "
        "values ($1::uuid, $2, '待校对', false) returning id", other_bridge_id, kInspectionYear);
    const auto other_year_id = other_year[0]["id"].as<std::string>();
    client_->execSqlSync(
        "update import_records set inspection_year_id = $2::uuid where id = $1::uuid",
        import_record_id_, other_year_id);

    bridge_report::db::ReviewRepository repository(client_);
    const auto outcome = repository.confirm_annual_facts(import_record_id_, false, "");

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "preflight_failed");
    EXPECT_EQ(outcome.preflight_details["blocking_errors"][0]["code"].asString(),
              "inspection_year_bridge_mismatch");
    client_->execSqlSync(
        "update import_records set inspection_year_id = $2::uuid where id = $1::uuid",
        import_record_id_, placeholder_year_id_);
    client_->execSqlSync("delete from inspection_years where id = $1::uuid", other_year_id);
    client_->execSqlSync("delete from bridges where id = $1::uuid", other_bridge_id);
}

TEST_F(ConfirmAnnualFactsTest, confirm_happy_path_writes_all_fact_tables) {
    bridge_report::db::ReviewRepository repository(client_);
    const auto outcome =
        repository.confirm_annual_facts(import_record_id_, /*confirm_revision=*/false, "首次入库确认");

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    EXPECT_TRUE(outcome.error_code.empty());
    EXPECT_EQ(outcome.inspection_year_id, placeholder_year_id_);
    EXPECT_EQ(outcome.version_number, 1);
    EXPECT_EQ(outcome.written.defect_observations, 1);
    EXPECT_EQ(outcome.written.defect_measurements, 3);
    EXPECT_EQ(outcome.written.defect_photos, 1);
    EXPECT_EQ(outcome.written.condition_ratings, 8);

    const auto observation_result = client_->execSqlSync(
        "select id, scale, defect_deduction, defect_description_raw, part_name, component_type, "
        "business_component_code, review_status, bridge_component_id "
        "from defect_observations where source_import_record_id = $1::uuid",
        import_record_id_
    );
    ASSERT_EQ(observation_result.size(), 1u);
    const auto observation_id = observation_result[0]["id"].as<std::string>();
    // severity="warning" 不再进入 scale；标度只来自 defect_scale=2。
    EXPECT_EQ(observation_result[0]["scale"].as<std::string>(), "2");
    EXPECT_DOUBLE_EQ(observation_result[0]["defect_deduction"].as<double>(), 35.0);
    EXPECT_EQ(observation_result[0]["defect_description_raw"].as<std::string>(), "梁底发现纵向裂缝，需现场复核。");
    EXPECT_EQ(observation_result[0]["part_name"].as<std::string>(), "上部承重构件");
    EXPECT_EQ(observation_result[0]["component_type"].as<std::string>(), "上部承重构件");
    EXPECT_EQ(observation_result[0]["business_component_code"].as<std::string>(), "主梁");
    EXPECT_EQ(observation_result[0]["review_status"].as<std::string>(), "已确认");

    const auto measurement_result = client_->execSqlSync(
        "select measurement_type from defect_measurements where defect_observation_id = $1::uuid", observation_id
    );
    EXPECT_EQ(measurement_result.size(), 3u);

    const auto photo_result = client_->execSqlSync(
        "select match_status, photo_number, archived_file_id from defect_photos where defect_observation_id = $1::uuid", observation_id
    );
    ASSERT_EQ(photo_result.size(), 1u);
    EXPECT_EQ(photo_result[0]["match_status"].as<std::string>(), "已确认");
    EXPECT_EQ(photo_result[0]["photo_number"].as<std::string>(), "2.1-1");
    EXPECT_FALSE(photo_result[0]["archived_file_id"].isNull());

    const auto rating_result = client_->execSqlSync(
        "select rating_level from condition_ratings where source_import_record_id = $1::uuid", import_record_id_
    );
    ASSERT_EQ(rating_result.size(), 8u);
    int overall_count = 0;
    int structure_count = 0;
    int part_count = 0;
    int component_count = 0;
    for (const auto& row : rating_result) {
        const auto level = row["rating_level"].as<std::string>();
        if (level == "全桥") {
            ++overall_count;
        } else if (level == "结构分部") {
            ++structure_count;
        } else if (level == "部件") {
            ++part_count;
        } else if (level == "构件") {
            ++component_count;
        }
    }
    EXPECT_EQ(overall_count, 1);
    EXPECT_EQ(structure_count, 3);
    EXPECT_EQ(part_count, 3);
    EXPECT_EQ(component_count, 1);

    // 构件级评分行：三值校验列全部落库并绑定正确构件。
    const auto component_rating_result = client_->execSqlSync(
        "select structure_part, bridge_component_id, rating_item_name, score, source_score, calculated_score, "
        "score_validation_status, score_resolution_reason, calculation_details_json::text as details, review_status "
        "from condition_ratings where source_import_record_id = $1::uuid and rating_level = '构件'",
        import_record_id_
    );
    ASSERT_EQ(component_rating_result.size(), 1u);
    EXPECT_EQ(component_rating_result[0]["structure_part"].as<std::string>(), "上部结构");
    EXPECT_EQ(component_rating_result[0]["rating_item_name"].as<std::string>(), "上部承重构件");
    EXPECT_DOUBLE_EQ(component_rating_result[0]["score"].as<double>(), 65.0);
    EXPECT_DOUBLE_EQ(component_rating_result[0]["source_score"].as<double>(), 65.0);
    EXPECT_DOUBLE_EQ(component_rating_result[0]["calculated_score"].as<double>(), 65.0);
    EXPECT_EQ(component_rating_result[0]["score_validation_status"].as<std::string>(), "一致");
    EXPECT_TRUE(component_rating_result[0]["score_resolution_reason"].isNull());
    const auto details = parse_json_text(component_rating_result[0]["details"].as<std::string>());
    EXPECT_EQ(details["standard"].asString(), "JTG/T H21-2011 4.1.1");
    ASSERT_EQ(details["ordered_deductions"].size(), 1u);
    EXPECT_DOUBLE_EQ(details["ordered_deductions"][0].asDouble(), 35.0);
    EXPECT_EQ(component_rating_result[0]["review_status"].as<std::string>(), "已确认");

    const auto year_result = client_->execSqlSync(
        "select status, is_current, overall_score, overall_grade from inspection_years where id = $1::uuid",
        outcome.inspection_year_id
    );
    ASSERT_EQ(year_result.size(), 1u);
    EXPECT_EQ(year_result[0]["status"].as<std::string>(), "已确认");
    EXPECT_TRUE(year_result[0]["is_current"].as<bool>());
    EXPECT_DOUBLE_EQ(year_result[0]["overall_score"].as<double>(), 85.61);
    EXPECT_EQ(year_result[0]["overall_grade"].as<std::string>(), "2类");

    const auto record_result = client_->execSqlSync(
        "select import_status, inspection_year_id, validation_result_json::text as validation_result_json "
        "from import_records where id = $1::uuid",
        import_record_id_
    );
    ASSERT_EQ(record_result.size(), 1u);
    EXPECT_EQ(record_result[0]["import_status"].as<std::string>(), "已确认");
    EXPECT_EQ(record_result[0]["inspection_year_id"].as<std::string>(), outcome.inspection_year_id);
    const auto validation_json = parse_json_text(record_result[0]["validation_result_json"].as<std::string>());
    EXPECT_EQ(validation_json["confirmation_note"].asString(), "首次入库确认");
    EXPECT_EQ(validation_json["written"]["defect_observations"].asInt(), 1);
    EXPECT_EQ(validation_json["written"]["defect_measurements"].asInt(), 3);
    EXPECT_EQ(validation_json["written"]["defect_photos"].asInt(), 1);
    EXPECT_EQ(validation_json["written"]["condition_ratings"].asInt(), 8);
    EXPECT_EQ(validation_json["written"]["component_condition_ratings"].asInt(), 1);

    const auto component_result = client_->execSqlSync(
        "select id, structure_part, component_type, business_component_code, current_status, creation_source "
        "from bridge_components where bridge_id = $1::uuid",
        bridge_id_
    );
    ASSERT_EQ(component_result.size(), 1u);
    EXPECT_EQ(component_result[0]["structure_part"].as<std::string>(), "上部结构");
    EXPECT_EQ(component_result[0]["component_type"].as<std::string>(), "上部承重构件");
    EXPECT_EQ(component_result[0]["business_component_code"].as<std::string>(), "主梁");
    EXPECT_EQ(component_result[0]["current_status"].as<std::string>(), "已确认");
    EXPECT_EQ(component_result[0]["creation_source"].as<std::string>(), "导入沉淀");
    const auto component_id = component_result[0]["id"].as<std::string>();
    EXPECT_EQ(observation_result[0]["bridge_component_id"].as<std::string>(), component_id);

    const auto alias_result = client_->execSqlSync(
        "select alias_text, is_manually_confirmed from component_aliases where bridge_component_id = $1::uuid",
        component_id
    );
    ASSERT_EQ(alias_result.size(), 1u);
    EXPECT_EQ(alias_result[0]["alias_text"].as<std::string>(), "上部承重构件");
    EXPECT_TRUE(alias_result[0]["is_manually_confirmed"].as<bool>());
}

TEST_F(ConfirmAnnualFactsTest, confirm_requires_revision_when_current_facts_exist) {
    const auto current_year_result = client_->execSqlSync(
        "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current) "
        "values ($1::uuid, $2, '已确认', 1, true) returning id",
        bridge_id_,
        kInspectionYear
    );
    const auto current_year_id = current_year_result[0]["id"].as<std::string>();
    tracked_year_ids_.push_back(current_year_id);

    bridge_report::db::ReviewRepository repository(client_);
    const auto blocked_outcome =
        repository.confirm_annual_facts(import_record_id_, /*confirm_revision=*/false, "");

    EXPECT_FALSE(blocked_outcome.success);
    EXPECT_EQ(blocked_outcome.error_code, "revision_confirmation_required");

    const auto observation_count_before = client_->execSqlSync(
        "select count(*) as n from defect_observations where source_import_record_id = $1::uuid", import_record_id_
    );
    EXPECT_EQ(observation_count_before[0]["n"].as<int64_t>(), 0);
    const auto record_still_pending = client_->execSqlSync(
        "select import_status from import_records where id = $1::uuid", import_record_id_
    );
    EXPECT_EQ(record_still_pending[0]["import_status"].as<std::string>(), "待校对");

    const auto confirmed_outcome =
        repository.confirm_annual_facts(import_record_id_, /*confirm_revision=*/true, "修订确认");

    ASSERT_TRUE(confirmed_outcome.success) << confirmed_outcome.error_code << ": " << confirmed_outcome.error_message;
    tracked_year_ids_.push_back(confirmed_outcome.inspection_year_id);
    EXPECT_NE(confirmed_outcome.inspection_year_id, current_year_id);
    EXPECT_NE(confirmed_outcome.inspection_year_id, placeholder_year_id_);
    EXPECT_EQ(confirmed_outcome.version_number, 2);

    const auto old_row = client_->execSqlSync(
        "select status, is_current from inspection_years where id = $1::uuid", current_year_id
    );
    ASSERT_EQ(old_row.size(), 1u);
    EXPECT_EQ(old_row[0]["status"].as<std::string>(), "已被修订");
    EXPECT_FALSE(old_row[0]["is_current"].as<bool>());

    const auto new_row = client_->execSqlSync(
        "select version_number, is_current, status, revision_source_inspection_id "
        "from inspection_years where id = $1::uuid",
        confirmed_outcome.inspection_year_id
    );
    ASSERT_EQ(new_row.size(), 1u);
    EXPECT_EQ(new_row[0]["version_number"].as<int>(), 2);
    EXPECT_TRUE(new_row[0]["is_current"].as<bool>());
    EXPECT_EQ(new_row[0]["status"].as<std::string>(), "已确认");
    EXPECT_EQ(new_row[0]["revision_source_inspection_id"].as<std::string>(), current_year_id);

    const auto observation_after = client_->execSqlSync(
        "select inspection_year_id from defect_observations where source_import_record_id = $1::uuid",
        import_record_id_
    );
    ASSERT_EQ(observation_after.size(), 1u);
    EXPECT_EQ(observation_after[0]["inspection_year_id"].as<std::string>(), confirmed_outcome.inspection_year_id);

    // 构件评分绑定新版本检测行；同构件唯一约束按检测版本作用域，与旧版本不冲突。
    const auto component_rating_after = client_->execSqlSync(
        "select inspection_year_id from condition_ratings "
        "where source_import_record_id = $1::uuid and rating_level = '构件'",
        import_record_id_
    );
    ASSERT_EQ(component_rating_after.size(), 1u);
    EXPECT_EQ(component_rating_after[0]["inspection_year_id"].as<std::string>(),
              confirmed_outcome.inspection_year_id);

    const auto record_after = client_->execSqlSync(
        "select inspection_year_id, import_status from import_records where id = $1::uuid", import_record_id_
    );
    ASSERT_EQ(record_after.size(), 1u);
    EXPECT_EQ(record_after[0]["inspection_year_id"].as<std::string>(), confirmed_outcome.inspection_year_id);
    EXPECT_EQ(record_after[0]["import_status"].as<std::string>(), "已确认");

    // 被遗弃的挂载占位行（原 import_records.inspection_year_id）应在修订入库时随事务一并删除，
    // 不能残留为死数据。记录现已改指向新版本行 Z，占位行 X 无人引用，必须已消失。
    const auto placeholder_after = client_->execSqlSync(
        "select count(*) as n from inspection_years where id = $1::uuid", placeholder_year_id_
    );
    EXPECT_EQ(placeholder_after[0]["n"].as<int64_t>(), 0);
}

TEST_F(ConfirmAnnualFactsTest, confirm_blocks_wrong_status) {
    client_->execSqlSync("update import_records set import_status = '已确认' where id = $1::uuid", import_record_id_);

    bridge_report::db::ReviewRepository repository(client_);
    const auto outcome =
        repository.confirm_annual_facts(import_record_id_, /*confirm_revision=*/false, "");

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "import_record_wrong_status");

    const auto observation_count = client_->execSqlSync(
        "select count(*) as n from defect_observations where source_import_record_id = $1::uuid", import_record_id_
    );
    EXPECT_EQ(observation_count[0]["n"].as<int64_t>(), 0);
}

TEST_F(ConfirmAnnualFactsTest, duplicate_component_rating_is_blocked_without_partial_writes) {
    bridge_report::db::ReviewRepository repository(client_);
    auto data = build_confirmed_data();
    Json::Value duplicate = data["ratings"]["component_ratings"][0];
    duplicate["candidate_id"] = "component_rating_0002";
    data["ratings"]["component_ratings"].append(duplicate);
    ASSERT_TRUE(repository.save_review_draft(import_record_id_, write_json_compact(data)));

    const auto outcome = repository.confirm_annual_facts(import_record_id_, /*confirm_revision=*/false, "");

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "preflight_failed");
    bool found = false;
    for (const auto& issue : outcome.preflight_details["blocking_errors"]) {
        if (issue["code"].asString() == "component_rating_duplicate_component") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
    const auto rating_count = client_->execSqlSync(
        "select count(*) as n from condition_ratings where source_import_record_id = $1::uuid", import_record_id_
    );
    EXPECT_EQ(rating_count[0]["n"].as<int64_t>(), 0);
    const auto record_row = client_->execSqlSync(
        "select import_status from import_records where id = $1::uuid", import_record_id_
    );
    EXPECT_EQ(record_row[0]["import_status"].as<std::string>(), "待校对");
}

TEST_F(ConfirmAnnualFactsTest, confirm_rolls_back_on_failure) {
    bridge_report::db::ReviewRepository repository(client_);
    auto data = build_confirmed_data();
    // 契约/预检允许非空结构部位，但数据库 bridge_components 的枚举约束会拒绝它，
    // 用于验证写入中途异常仍回滚整笔事务。
    data["defects"][0]["structure_part"] = "不存在的结构";
    ASSERT_TRUE(repository.save_review_draft(import_record_id_, write_json_compact(data)));

    const auto outcome =
        repository.confirm_annual_facts(import_record_id_, /*confirm_revision=*/false, "");

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "db_write_failed");
    EXPECT_FALSE(outcome.error_message.empty());

    const auto observation_count = client_->execSqlSync(
        "select count(*) as n from defect_observations where source_import_record_id = $1::uuid", import_record_id_
    );
    EXPECT_EQ(observation_count[0]["n"].as<int64_t>(), 0);
    // defect_measurements / defect_photos 也应当零行。虽然它们经 defect_observation_id 外键级联，
    // 观测行为零即隐含子行为零，这里仍显式断言，避免回滚不彻底时被 FK 级联掩盖。
    const auto measurement_count = client_->execSqlSync(
        "select count(*) as n from defect_measurements dm "
        "join defect_observations do2 on do2.id = dm.defect_observation_id "
        "where do2.source_import_record_id = $1::uuid",
        import_record_id_
    );
    EXPECT_EQ(measurement_count[0]["n"].as<int64_t>(), 0);
    const auto photo_count = client_->execSqlSync(
        "select count(*) as n from defect_photos where source_import_record_id = $1::uuid", import_record_id_
    );
    EXPECT_EQ(photo_count[0]["n"].as<int64_t>(), 0);
    const auto rating_count = client_->execSqlSync(
        "select count(*) as n from condition_ratings where source_import_record_id = $1::uuid", import_record_id_
    );
    EXPECT_EQ(rating_count[0]["n"].as<int64_t>(), 0);
    const auto component_count = client_->execSqlSync(
        "select count(*) as n from bridge_components where bridge_id = $1::uuid", bridge_id_
    );
    EXPECT_EQ(component_count[0]["n"].as<int64_t>(), 0);

    const auto year_row = client_->execSqlSync(
        "select status, is_current, overall_score from inspection_years where id = $1::uuid", placeholder_year_id_
    );
    ASSERT_EQ(year_row.size(), 1u);
    EXPECT_EQ(year_row[0]["status"].as<std::string>(), "待校对");
    EXPECT_FALSE(year_row[0]["is_current"].as<bool>());
    EXPECT_TRUE(year_row[0]["overall_score"].isNull());

    const auto record_row = client_->execSqlSync(
        "select import_status from import_records where id = $1::uuid", import_record_id_
    );
    ASSERT_EQ(record_row.size(), 1u);
    EXPECT_EQ(record_row[0]["import_status"].as<std::string>(), "待校对");
}
