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
