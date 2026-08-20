#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <drogon/orm/DbClient.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/EditLockRepository.hpp"
#include "bridge_report/db/RatingTreeRepository.hpp"
#include "bridge_report/assessment/AssessmentConfirmationService.hpp"
#include "bridge_report/db/ReviewRepository.hpp"
#include "bridge_report/db/StandardRepository.hpp"
#include "bridge_report/rating_tree/RatingTreeCompiler.hpp"
#include "bridge_report/rating_tree/RatingTreePackageLoader.hpp"
#include "bridge_report/review/ConfirmPlan.hpp"
#include "bridge_report/review/PreflightReport.hpp"
#include "bridge_report/review/ReviewModels.hpp"
#include "bridge_report/standards/StandardPackageLoader.hpp"
#include "support/h21_fixtures.hpp"
#include "support/review_fixtures.hpp"

namespace {

using bridge_report::test_support::confirm_all_candidates;

std::string read_fixture_text(const std::string& file_name) {
    auto current_file_name = file_name;
    const auto version_marker = current_file_name.find(".v2.");
    if (version_marker != std::string::npos) {
        current_file_name.replace(version_marker, 4, ".v4.");
    }
    const auto path = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) / "samples" / "contracts" / current_file_name;
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
    const auto fixture_json = read_fixture_text("bridge_annual_inspection_data.v2.valid.json");
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
    EXPECT_FALSE(detail->inspection_year_inventory_revision_id.has_value());
}

// 校对保存、入库前检查与评定树自动匹配都要用"年度锁定优先"那条解析规则，而它的入参
// 就是这个字段。缺了它，三处只能各自去查一次，或者退回草稿优先的"桥梁最新版本"。
TEST_F(ReviewRepositoryTest, get_import_record_detail_carries_the_year_locked_inventory_revision) {
    bridge_report::db::ReviewRepository repository(tx_);

    // 年度尚未锁定版本时为空。
    EXPECT_FALSE(repository.get_import_record_detail(import_record_id_)
                     ->inspection_year_inventory_revision_id.has_value());

    // 触发器只允许正式年度挂已确认版本，所以先确认再挂。
    const auto revision_id = tx_->execSqlSync(
        "insert into bridge_component_inventory_revisions"
        "(bridge_id,revision_number,created_by_user_id) "
        "values($1::uuid,1,(select id from users where username='admin')) returning id::text",
        bridge_id_)[0]["id"].as<std::string>();
    tx_->execSqlSync(
        "update bridge_component_inventory_revisions set status='已确认',"
        "confirmed_by_user_id=(select id from users where username='admin'),confirmed_at=now() "
        "where id=$1::uuid",
        revision_id);
    tx_->execSqlSync(
        "update inspection_years set component_inventory_revision_id=$2::uuid where id=$1::uuid",
        inspection_year_id_, revision_id);

    const auto detail = repository.get_import_record_detail(import_record_id_);
    ASSERT_TRUE(detail->inspection_year_inventory_revision_id.has_value());
    EXPECT_EQ(*detail->inspection_year_inventory_revision_id, revision_id);
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
    const auto fixture_json = read_fixture_text("bridge_annual_inspection_data.v2.valid.json");

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

TEST_F(ReviewRepositoryTest, save_review_draft_appends_defect_change_audit_event) {
    const auto fixture_json = read_fixture_text("bridge_annual_inspection_data.v2.valid.json");
    Json::Value event(Json::objectValue);
    event["event_type"] = "draft_defect_structure_change";
    event["actor_username"] = "editor";
    event["added_candidate_ids"] = Json::Value(Json::arrayValue);
    event["added_candidate_ids"].append("manual_defect_uuid_1");
    event["deleted_candidate_ids"] = Json::Value(Json::arrayValue);

    bridge_report::db::ReviewRepository repository(tx_);
    ASSERT_TRUE(repository.save_review_draft(
        import_record_id_, fixture_json, std::nullopt, write_json_compact(event)));

    const auto row = tx_->execSqlSync(
        "select validation_result_json::text as audit from import_records where id = $1::uuid",
        import_record_id_);
    ASSERT_FALSE(row.empty());
    const auto audit = parse_json_text(row[0]["audit"].as<std::string>());
    ASSERT_TRUE(audit["draft_audit_events"].isArray());
    ASSERT_EQ(audit["draft_audit_events"].size(), 1u);
    EXPECT_EQ(audit["draft_audit_events"][0]["actor_username"].asString(), "editor");
    EXPECT_TRUE(audit["draft_audit_events"][0]["saved_at"].isString());
}

TEST_F(ReviewRepositoryTest, save_review_draft_returns_false_and_leaves_json_when_not_pending_review) {
    tx_->execSqlSync(
        "update import_records set import_status = $1 where id = $2::uuid",
        "已取消",
        import_record_id_
    );
    const auto fixture_json = read_fixture_text("bridge_annual_inspection_data.v2.valid.json");

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

    auto data = parse_json_text(read_fixture_text("bridge_annual_inspection_data.v2.valid.json"));
    data["import_context"]["import_record_system_number"] = initial_detail->system_number;
    data["bridge_check"]["selected_bridge_system_number"] = initial_detail->bridge_system_number;
    ASSERT_TRUE(initial_detail->inspection_year.has_value());
    data["inspection"]["inspection_year"] = *initial_detail->inspection_year;

    // 样例默认病害候选是“待确认”，直接保存即可覆盖候选阻断场景。
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

    // 把病害和照片候选全部改为“已确认”后重新保存，再走一遍流程应当放行。
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

        const auto user = client_->execSqlSync(
            "insert into users(username,display_name,password_hash,role) "
            "values($1,'正式评定测试员','test','admin') returning id::text as id",
            "assessment_confirm_" + bridge_id_.substr(0, 8));
        confirmed_by_user_id_ = user[0]["id"].as<std::string>();
        tracked_user_ids_.push_back(confirmed_by_user_id_);

        registry_ = std::make_shared<bridge_report::standards::StandardRegistry>();
        auto registry_package = bridge_report::tests::h21::load_package();
        ASSERT_TRUE(registry_->register_package(std::move(registry_package)).accepted);

        const auto repository_root = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT);
        bridge_report::standards::StandardPackageLoader standard_loader;
        const auto h21_source = standard_loader.load(
            repository_root / "standards/technical-condition/jtg-t-h21-2011/1.0.2");
        const auto maintenance_source = standard_loader.load(
            repository_root / "standards/maintenance/jtg-5120-2021/1.0.0");
        bridge_report::rating_tree::RatingTreePackageLoader tree_loader;
        const auto tree_extension = tree_loader.load(
            repository_root / "standards/rating-tree/organization-bridge/1.0.1");
        ASSERT_TRUE(h21_source.ok());
        ASSERT_TRUE(maintenance_source.ok());
        ASSERT_TRUE(tree_extension.ok());

        bridge_report::db::StandardRepository standard_repository(client_);
        const auto technical =
            standard_repository.sync_package(h21_source.package->manifest);
        const auto maintenance =
            standard_repository.sync_package(maintenance_source.package->manifest);
        ASSERT_TRUE(technical.package_id.has_value());
        ASSERT_TRUE(maintenance.package_id.has_value());
        technical_package_id_ = *technical.package_id;
        maintenance_package_id_ = *maintenance.package_id;

        bridge_report::rating_tree::RatingTreeCompiler tree_compiler;
        const auto compiled_tree = tree_compiler.compile(
            *h21_source.package, &*maintenance_source.package, *tree_extension.package);
        ASSERT_TRUE(compiled_tree.ok());
        bridge_report::db::RatingTreeRepository tree_repository(client_);
        const auto tree_sync = tree_repository.sync_published_tree(*compiled_tree.tree);
        ASSERT_TRUE(tree_sync.rating_tree_version_id.has_value());
        rating_tree_version_id_ = *tree_sync.rating_tree_version_id;

        const auto profile = client_->execSqlSync(
            "insert into project_standard_profiles(technical_condition_package_id,maintenance_package_id,"
            "rating_tree_version_id,created_by_user_id,change_reason) "
            "values($1::uuid,$2::uuid,$3::uuid,$4::uuid,'正式评定测试') "
            "returning id::text as id",
            technical_package_id_, maintenance_package_id_, rating_tree_version_id_,
            confirmed_by_user_id_);
        standard_profile_id_ = profile[0]["id"].as<std::string>();

        const auto tree_node = client_->execSqlSync(
            "select id::text as id from rating_tree_nodes "
            "where rating_tree_version_id=$1::uuid "
            "and h21_indicator_id='h21.defect.5_3_1_1' "
            "and 'h21.bridge_type.beam'=any(bridge_type_ids) "
            "and 'h21.component.bearing'=any(component_category_ids) "
            "and is_selectable",
            rating_tree_version_id_);
        ASSERT_EQ(tree_node.size(), 1u);
        rating_tree_node_id_ = tree_node[0]["id"].as<std::string>();

        // 占位年度行：待校对 + is_current=false，与规格步骤 2 的初始状态一致；
        // is_current 必须显式置为 false（而非依赖列默认值 true），否则修订测试另外插入的
        // “已确认 + is_current” 行会与这一行同时违反 ux_inspection_years_current_bridge_year。
        const auto year_result = client_->execSqlSync(
            "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current,"
            "standard_profile_id) values ($1::uuid, $2, '待校对', 1, false,$3::uuid) returning id",
            bridge_id_,
            kInspectionYear,
            standard_profile_id_
        );
        placeholder_year_id_ = year_result[0]["id"].as<std::string>();
        tracked_year_ids_.push_back(placeholder_year_id_);

        const auto inventory = client_->execSqlSync(
            "insert into bridge_component_inventory_revisions(bridge_id,revision_number,created_by_user_id) "
            "values($1::uuid,1,$2::uuid) returning id::text as id",
            bridge_id_, confirmed_by_user_id_);
        inventory_revision_id_ = inventory[0]["id"].as<std::string>();
        const auto loaded_package = bridge_report::tests::h21::load_package();
        const auto input = bridge_report::tests::h21::complete_beam_input(loaded_package);
        const bridge_report::standards::H21Evaluator evaluator(loaded_package);
        const auto evaluated = evaluator.evaluate(input);
        ASSERT_TRUE(evaluated.result.has_value());
        int sort_order = 0;
        for (const auto& part : evaluated.result->structure_parts) {
            for (const auto& category : part.categories) {
                const auto component_number = "TEST-" + std::to_string(++sort_order);
                const std::string structure_part = part.structure_part == bridge_report::standards::StructurePart::superstructure
                    ? std::string("上部结构")
                    : part.structure_part == bridge_report::standards::StructurePart::substructure
                        ? std::string("下部结构") : std::string("桥面系");
                const auto component = client_->execSqlSync(
                    "insert into bridge_components(bridge_id,structure_part,component_type,"
                    "business_component_code,normalized_component_key,current_status,creation_source) "
                    "values($1::uuid,$2,$3,$4,$5,'已确认','人工录入') returning id::text as id",
                    bridge_id_, structure_part, category.component_type_id, component_number,
                    structure_part + "|" + category.component_type_id + "|" + component_number);
                const auto component_id = component[0]["id"].as<std::string>();
                component_id_by_type_[category.component_type_id] = component_id;
                const auto entry = client_->execSqlSync(
                    "insert into bridge_component_inventory_entries(inventory_revision_id,bridge_component_id,"
                    "component_number,site_name,site_component_type,sort_order) "
                    "values($1::uuid,$2::uuid,$3,$3,$4,$5) returning id::text as id",
                    inventory_revision_id_, component_id, component_number,
                    category.component_type_id, sort_order);
                client_->execSqlSync(
                    "insert into bridge_component_standard_mappings(inventory_entry_id,standard_package_id,"
                    "standard_bridge_type_id,standard_component_category_id,structure_part,mapping_source,"
                    "confirmation_status,confirmed_by_user_id,confirmed_at) "
                    "values($1::uuid,$2::uuid,$3,$4,$5,'测试','已确认',$6::uuid,now())",
                    entry[0]["id"].as<std::string>(), technical_package_id_, input.bridge_type_id,
                    category.component_type_id,
                    bridge_report::standards::to_string(part.structure_part), confirmed_by_user_id_);
            }
        }
        client_->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认',"
            "confirmed_by_user_id=$2::uuid,confirmed_at=now(),confirmation_note='测试确认' "
            "where id=$1::uuid",
            inventory_revision_id_, confirmed_by_user_id_);
        client_->execSqlSync(
            "update inspection_years set component_inventory_revision_id=$2::uuid where id=$1::uuid",
            placeholder_year_id_, inventory_revision_id_);

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
        // 正式评定在生产中永久不可删；集成测试必须回收隔离夹具数据，因此仅在
        // 精确删除本夹具年度的运行期间短暂关闭保护触发器，随后立即恢复。
        for (auto it = tracked_year_ids_.rbegin(); it != tracked_year_ids_.rend(); ++it) {
            client_->execSqlSync(
                "delete from condition_ratings where inspection_year_id=$1::uuid", *it);
        }
        client_->execSqlSync(
            "alter table assessment_runs disable trigger trg_assessment_runs_completed_formal_immutable");
        for (auto it = tracked_year_ids_.rbegin(); it != tracked_year_ids_.rend(); ++it) {
            client_->execSqlSync(
                "delete from assessment_runs where inspection_year_id=$1::uuid", *it);
        }
        client_->execSqlSync(
            "alter table assessment_runs enable trigger trg_assessment_runs_completed_formal_immutable");
        for (auto it = tracked_year_ids_.rbegin(); it != tracked_year_ids_.rend(); ++it) {
            client_->execSqlSync("delete from inspection_years where id = $1::uuid", *it);
        }
        if (!bridge_id_.empty()) {
            client_->execSqlSync("delete from bridges where id = $1::uuid", bridge_id_);
        }
        if (!standard_profile_id_.empty()) {
            client_->execSqlSync(
                "delete from project_standard_profiles where id=$1::uuid", standard_profile_id_);
        }
        for (const auto& user_id : tracked_user_ids_) {
            client_->execSqlSync("delete from users where id = $1::uuid", user_id);
        }
        // 显式关闭连接，而不是让 client_ 在夹具析构时才隐式释放——本 fixture（不同于
        // ReviewRepositoryTest）在裸 client_ 上做了大量各自独立提交的语句，外加
        // confirm_annual_facts 内部另开的事务，连接/事务生命周期的churn 明显更高；
        // 让析构前的关闭时机确定下来，避免进程退出阶段与 drogon 内部异步清理产生竞争。
        client_->closeAll();
    }

    // 组装与本 fixture 标识对齐、且病害组已经确认的 4.0 样例数据。
    Json::Value build_confirmed_data() const {
        auto data = parse_json_text(read_fixture_text("bridge_annual_inspection_data.v2.valid.json"));
        data["import_context"]["import_record_system_number"] = import_record_system_number_;
        data["bridge_check"]["selected_bridge_system_number"] = bridge_system_number_;
        data["inspection"]["inspection_year"] = kInspectionYear;
        confirm_all_candidates(data);
        const auto bearing = component_id_by_type_.find("h21.component.bearing");
        if (bearing == component_id_by_type_.end()) {
            throw std::runtime_error("test inventory is missing bearing component");
        }
        data["defects"][0]["bridge_component_id"] = bearing->second;
        data["defects"][0]["standard_component_category_id"] = "h21.component.bearing";
        data["defects"][0]["component_inventory_revision_id"] = inventory_revision_id_;
        data["defects"][0]["source_structure_part"] = "上部结构";
        data["defects"][0]["resolved_structure_part"] = "上部结构";
        data["defects"][0]["component_name"] = "支座";
        data["defects"][0]["component_number"] = "TEST-BEARING";
        data["defects"][0]["defect_type"] = "板式支座老化变质、开裂";
        data["defects"][0]["standard_defect_indicator_id"] =
            "h21.defect.5_3_1_1";
        data["defects"][0]["rating_tree_version_id"] = rating_tree_version_id_;
        data["defects"][0]["rating_tree_node_id"] = rating_tree_node_id_;
        data["defects"][0]["rating_tree_match_method"] = "exact";
        data["defects"][0]["rating_tree_match_evidence"] =
            "正式评定集成测试使用已发布评定树节点";
        data["defects"][0]["defect_scale"] = 2;
        return data;
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string bridge_system_number_;
    std::string import_record_id_;
    std::string import_record_system_number_;
    std::string placeholder_year_id_;
    std::string photo_archived_file_id_;
    std::string confirmed_by_user_id_;
    std::string technical_package_id_;
    std::string maintenance_package_id_;
    std::string rating_tree_version_id_;
    std::string rating_tree_node_id_;
    std::string standard_profile_id_;
    std::string inventory_revision_id_;
    std::shared_ptr<bridge_report::standards::StandardRegistry> registry_;
    std::map<std::string, std::string> component_id_by_type_;
    std::vector<std::string> tracked_year_ids_;
    std::vector<std::string> tracked_user_ids_;
};

}  // 匿名命名空间

// 评定服务自己重读年度版本，而且是内连接：年度没锁版本就取不到上下文行，直接
// assessment_context_incomplete。所以只把六处解析改对并不闭环——入库前检查还得能把
// 解析出的版本显式递进来，且**不能**为此去写年度。
TEST_F(ConfirmAnnualFactsTest, ReadOnlyPreflightAcceptsAnExplicitRevisionWithoutLockingTheYear) {
    // 让年度回到"未锁定版本"的状态。
    client_->execSqlSync(
        "update inspection_years set component_inventory_revision_id=null where id=$1::uuid",
        placeholder_year_id_);

    bridge_report::assessment::AssessmentConfirmationService service(client_, registry_);
    const auto without_override =
        service.calculate(placeholder_year_id_, build_confirmed_data());
    bool incomplete = false;
    for (const auto& item : without_override.preview.issues) {
        if (item.code == "assessment_context_incomplete") incomplete = true;
    }
    EXPECT_TRUE(incomplete) << "年度未锁版本且不给 override 时，维持原有的上下文不完整";

    const auto with_override = service.calculate(
        placeholder_year_id_, build_confirmed_data(),
        std::optional<std::string>(inventory_revision_id_));
    for (const auto& item : with_override.preview.issues) {
        EXPECT_NE(item.code, "assessment_context_incomplete")
            << "给了合法版本就该能构建上下文：" << item.message;
    }

    // 只读预检绝不能顺手把版本锁进年度。
    const auto year = client_->execSqlSync(
        "select component_inventory_revision_id::text as revision_id "
        "from inspection_years where id=$1::uuid", placeholder_year_id_);
    EXPECT_TRUE(year[0]["revision_id"].isNull()) << "预检写了年度版本";
}

// override 不是绕过校验的后门，也不允许静默盖过年度已锁定的版本。
TEST_F(ConfirmAnnualFactsTest, ExplicitRevisionIsValidatedAndCannotOverrideALockedYear) {
    bridge_report::assessment::AssessmentConfirmationService service(client_, registry_);

    // 年度锁着 inventory_revision_id_，此时给一个不同的版本 → 明确报版本变化。
    const auto other_revision = client_->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
        "created_by_user_id) values($1::uuid,2,$2::uuid) returning id::text as id",
        bridge_id_, confirmed_by_user_id_)[0]["id"].as<std::string>();
    client_->execSqlSync(
        "update bridge_component_inventory_revisions set status='已确认',"
        "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
        other_revision, confirmed_by_user_id_);

    const auto conflicting = service.calculate(
        placeholder_year_id_, build_confirmed_data(), std::optional<std::string>(other_revision));
    bool reported_change = false;
    for (const auto& item : conflicting.preview.issues) {
        if (item.code == "component_inventory_revision_changed") reported_change = true;
    }
    EXPECT_TRUE(reported_change)
        << "年度锁定版本与 override 不一致时必须报出来，不能静默取其一";

    // 属于别的桥的版本不可用。
    const auto other_bridge = client_->execSqlSync(
        "insert into bridges(bridge_name) values('评定override测试桥') returning id::text as id"
        )[0]["id"].as<std::string>();
    const auto foreign_revision = client_->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
        "created_by_user_id) values($1::uuid,1,$2::uuid) returning id::text as id",
        other_bridge, confirmed_by_user_id_)[0]["id"].as<std::string>();
    client_->execSqlSync(
        "update bridge_component_inventory_revisions set status='已确认',"
        "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
        foreign_revision, confirmed_by_user_id_);

    const auto foreign = service.calculate(
        placeholder_year_id_, build_confirmed_data(), std::optional<std::string>(foreign_revision));
    bool rejected = false;
    for (const auto& item : foreign.preview.issues) {
        if (item.code == "assessment_context_incomplete") rejected = true;
    }
    EXPECT_TRUE(rejected) << "别的桥的台账版本不得被接受";

    client_->execSqlSync("delete from bridge_component_inventory_revisions where id=$1::uuid",
                         foreign_revision);
    client_->execSqlSync("delete from bridges where id=$1::uuid", other_bridge);
}

// 确认事务此前另起一条草稿优先解析喂给评定树校验，桥上一有草稿就按草稿的映射判。
// preflight 那半本来就用年度锁定版本，两半用的不是同一份台账。
TEST_F(ConfirmAnnualFactsTest, ConfirmUsesTheConfirmedRevisionWhileADraftExists) {
    // 派生一个版本号更大的草稿：草稿优先的排序会挑中它。
    client_->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
        "baseline_revision_id,created_by_user_id) values($1::uuid,2,$2::uuid,$3::uuid)",
        bridge_id_, inventory_revision_id_, confirmed_by_user_id_);

    bridge_report::db::ReviewRepository repository(client_, registry_);
    ASSERT_TRUE(repository.save_review_draft(
        import_record_id_, write_json_compact(build_confirmed_data())));
    const auto outcome = repository.confirm_annual_facts(
        import_record_id_, false, "草稿共存时确认", confirmed_by_user_id_);

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    const auto year = client_->execSqlSync(
        "select component_inventory_revision_id::text as revision_id from inspection_years "
        "where id=$1::uuid", placeholder_year_id_);
    EXPECT_EQ(year[0]["revision_id"].as<std::string>(), inventory_revision_id_)
        << "确认后年度仍应指向已确认版本，而不是那个草稿";
}

// 年度尚未锁版本、而桥上有可用的已确认台账时，确认事务在 preflight 之前就把版本锁上。
// 锁在评定服务之前是不够的——preflight 判出"台账未确认"就直接返回了，根本走不到那里。
TEST_F(ConfirmAnnualFactsTest, ConfirmLocksTheResolvedRevisionBeforePreflight) {
    client_->execSqlSync(
        "update inspection_years set component_inventory_revision_id=null where id=$1::uuid",
        placeholder_year_id_);

    bridge_report::db::ReviewRepository repository(client_, registry_);
    ASSERT_TRUE(repository.save_review_draft(
        import_record_id_, write_json_compact(build_confirmed_data())));
    const auto outcome = repository.confirm_annual_facts(
        import_record_id_, false, "年度未锁版本时确认", confirmed_by_user_id_);

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    const auto year = client_->execSqlSync(
        "select component_inventory_revision_id::text as revision_id from inspection_years "
        "where id=$1::uuid", placeholder_year_id_);
    ASSERT_FALSE(year[0]["revision_id"].isNull()) << "确认事务应当把解析出的版本锁进年度";
    EXPECT_EQ(year[0]["revision_id"].as<std::string>(), inventory_revision_id_);
}

TEST_F(ConfirmAnnualFactsTest, ReadsLatestJsonInsteadOfCallerSnapshot) {
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto latest = build_confirmed_data();
    latest["defects"][0]["defect_description"] = "事务内最新病害描述";
    ASSERT_TRUE(repository.save_review_draft(import_record_id_, write_json_compact(latest)));

    const auto outcome = repository.confirm_annual_facts(
        import_record_id_, false, "确认最新草稿", confirmed_by_user_id_);

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    const auto rows = client_->execSqlSync(
        "select defect_description_raw from defect_observations where source_import_record_id = $1::uuid",
        import_record_id_);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0]["defect_description_raw"].as<std::string>(), "事务内最新病害描述");
}

TEST_F(ConfirmAnnualFactsTest, ConfirmationRechecksAndReleasesEditLockInsideFactTransaction) {
    const auto user_rows = client_->execSqlSync(
        "insert into users (username, display_name, password_hash, role) "
        "values ($1, '确认锁测试员', 'test', 'normal') returning id::text as id",
        "m06_confirm_lock_" + import_record_id_.substr(0, 8));
    const auto user_id = user_rows[0]["id"].as<std::string>();
    tracked_user_ids_.push_back(user_id);
    const auto session_rows = client_->execSqlSync(
        "insert into user_sessions (user_id, token_hash, expires_at) "
        "values ($1::uuid, $2, now() + interval '1 hour') returning id::text as id",
        user_id, "m06-confirm-session-" + import_record_id_);
    bridge_report::db::AuthUser user{
        user_id,
        session_rows[0]["id"].as<std::string>(),
        "m06_confirm_lock",
        "确认锁测试员",
        "normal",
    };
    bridge_report::db::EditLockRepository lock_repository(client_);
    const auto acquired = lock_repository.acquire(import_record_id_, user);
    ASSERT_TRUE(acquired.acquired);

    bridge_report::db::ReviewRepository repository(client_, registry_);
    const auto outcome = repository.confirm_annual_facts(
        import_record_id_,
        false,
        "确认并释放锁",
        user.id,
        bridge_report::db::EditLockCredentials{user.id, user.session_id, acquired.lock_token});

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    EXPECT_FALSE(lock_repository.get_active(import_record_id_).has_value());
    const auto state = client_->execSqlSync(
        "select import_status from import_records where id = $1::uuid", import_record_id_);
    EXPECT_EQ(state[0]["import_status"].as<std::string>(), "已确认");
}

TEST_F(ConfirmAnnualFactsTest, InvalidEditLockRollsBackBeforeWritingFacts) {
    const auto user_rows = client_->execSqlSync(
        "insert into users (username, display_name, password_hash, role) "
        "values ($1, '错误锁测试员', 'test', 'normal') returning id::text as id",
        "m06_invalid_lock_" + import_record_id_.substr(0, 8));
    const auto user_id = user_rows[0]["id"].as<std::string>();
    tracked_user_ids_.push_back(user_id);
    const auto session_rows = client_->execSqlSync(
        "insert into user_sessions (user_id, token_hash, expires_at) "
        "values ($1::uuid, $2, now() + interval '1 hour') returning id::text as id",
        user_id, "m06-invalid-session-" + import_record_id_);

    bridge_report::db::ReviewRepository repository(client_, registry_);
    const auto outcome = repository.confirm_annual_facts(
        import_record_id_,
        false,
        "不应入库",
        user_id,
        bridge_report::db::EditLockCredentials{
            user_id, session_rows[0]["id"].as<std::string>(), "not-a-real-lock-token"});

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "edit_lock_invalid");
    const auto state = client_->execSqlSync(
        "select import_status from import_records where id = $1::uuid", import_record_id_);
    EXPECT_EQ(state[0]["import_status"].as<std::string>(), "待校对");
    const auto facts = client_->execSqlSync(
        "select count(*) as count from defect_observations where source_import_record_id = $1::uuid",
        import_record_id_);
    EXPECT_EQ(facts[0]["count"].as<long long>(), 0);
}

TEST_F(ConfirmAnnualFactsTest, ReturnsTransactionTimePreflightDetailsForPendingLatestDraft) {
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto latest = build_confirmed_data();
    latest["defects"][0]["review_status"] = "待确认";
    ASSERT_TRUE(repository.save_review_draft(import_record_id_, write_json_compact(latest)));

    const auto outcome = repository.confirm_annual_facts(
        import_record_id_, false, "", confirmed_by_user_id_);

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "preflight_failed");
    EXPECT_FALSE(outcome.preflight_details["can_confirm"].asBool());
    EXPECT_FALSE(outcome.preflight_details["blocking_errors"].empty());
}

TEST_F(ConfirmAnnualFactsTest, MissingCurrentImportPhotoLinkBlocksAndRollsBack) {
    bridge_report::db::ReviewRepository repository(client_, registry_);
    client_->execSqlSync(
        "delete from import_record_files where import_record_id = $1::uuid and archived_file_id = $2::uuid",
        import_record_id_, photo_archived_file_id_);

    const auto outcome = repository.confirm_annual_facts(
        import_record_id_, false, "", confirmed_by_user_id_);

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

    bridge_report::db::ReviewRepository repository(client_, registry_);
    const auto outcome = repository.confirm_annual_facts(
        import_record_id_, false, "", confirmed_by_user_id_);

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
    bridge_report::db::ReviewRepository repository(client_, registry_);
    const auto outcome =
        repository.confirm_annual_facts(
            import_record_id_, /*confirm_revision=*/false, "首次入库确认", confirmed_by_user_id_);

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    EXPECT_TRUE(outcome.error_code.empty());
    EXPECT_EQ(outcome.inspection_year_id, placeholder_year_id_);
    EXPECT_EQ(outcome.version_number, 1);
    EXPECT_EQ(outcome.written.defect_observations, 1);
    EXPECT_EQ(outcome.written.defect_measurements, 3);
    EXPECT_EQ(outcome.written.defect_photos, 1);
    EXPECT_FALSE(outcome.assessment_run_id.empty());
    EXPECT_GT(outcome.written.condition_ratings, 0);
    EXPECT_EQ(
        outcome.written.assessment_component_results,
        static_cast<int>(component_id_by_type_.size()));
    EXPECT_GT(outcome.written.assessment_part_results, 0);
    EXPECT_GT(outcome.written.assessment_rule_traces, 0);

    const auto observation_result = client_->execSqlSync(
        "select id, scale, defect_description_raw, part_name, component_type, "
        "business_component_code, review_status, bridge_component_id, "
        "standard_defect_indicator_id "
        "from defect_observations where source_import_record_id = $1::uuid",
        import_record_id_
    );
    ASSERT_EQ(observation_result.size(), 1u);
    const auto observation_id = observation_result[0]["id"].as<std::string>();
    // severity="warning" 不再进入 scale；标度只来自 defect_scale=2。
    EXPECT_EQ(observation_result[0]["scale"].as<std::string>(), "2");
    EXPECT_EQ(observation_result[0]["defect_description_raw"].as<std::string>(), "梁底发现纵向裂缝，需现场复核。");
    EXPECT_EQ(observation_result[0]["part_name"].as<std::string>(), "TEST-BEARING");
    EXPECT_EQ(observation_result[0]["component_type"].as<std::string>(), "h21.component.bearing");
    EXPECT_FALSE(observation_result[0]["business_component_code"].as<std::string>().empty());
    EXPECT_EQ(observation_result[0]["review_status"].as<std::string>(), "已确认");
    EXPECT_EQ(
        observation_result[0]["standard_defect_indicator_id"].as<std::string>(),
        "h21.defect.5_3_1_1");

    const auto measurement_result = client_->execSqlSync(
        "select measurement_type from defect_measurements where defect_observation_id = $1::uuid", observation_id
    );
    EXPECT_EQ(measurement_result.size(), 3u);

    const auto photo_result = client_->execSqlSync(
        "select photo_number, archived_file_id from defect_photos where defect_observation_id = $1::uuid", observation_id
    );
    ASSERT_EQ(photo_result.size(), 1u);
    EXPECT_EQ(photo_result[0]["photo_number"].as<std::string>(), "2.1-1");
    EXPECT_FALSE(photo_result[0]["archived_file_id"].isNull());

    const auto rating_result = client_->execSqlSync(
        "select rating_level from condition_ratings where source_import_record_id = $1::uuid", import_record_id_
    );
    ASSERT_EQ(rating_result.size(), static_cast<std::size_t>(outcome.written.condition_ratings));
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
    EXPECT_GT(part_count, 0);
    EXPECT_EQ(component_count, static_cast<int>(component_id_by_type_.size()));

    // 构件级投影只保存系统结果和正式运行引用。
    const auto component_rating_result = client_->execSqlSync(
        "select structure_part, bridge_component_id, rating_item_name, score, "
        "assessment_run_id::text as assessment_run_id, review_status "
        "from condition_ratings where source_import_record_id = $1::uuid and rating_level = '构件'",
        import_record_id_
    );
    ASSERT_EQ(component_rating_result.size(), component_id_by_type_.size());
    for (const auto& row : component_rating_result) {
        EXPECT_EQ(row["assessment_run_id"].as<std::string>(), outcome.assessment_run_id);
        EXPECT_EQ(row["review_status"].as<std::string>(), "已确认");
    }

    const auto formal_run = client_->execSqlSync(
        "select run_kind,result_status,is_current,input_checksum,rule_package_checksum,"
        "technical_condition_package_id::text as package_id,standard_profile_id::text as profile_id,"
        "component_inventory_revision_id::text as inventory_id,confirmed_by_user_id::text as user_id "
        "from assessment_runs where id=$1::uuid",
        outcome.assessment_run_id);
    ASSERT_EQ(formal_run.size(), 1u);
    EXPECT_EQ(formal_run[0]["run_kind"].as<std::string>(), "正式");
    EXPECT_EQ(formal_run[0]["result_status"].as<std::string>(), "成功");
    EXPECT_TRUE(formal_run[0]["is_current"].as<bool>());
    EXPECT_EQ(formal_run[0]["package_id"].as<std::string>(), technical_package_id_);
    EXPECT_EQ(formal_run[0]["profile_id"].as<std::string>(), standard_profile_id_);
    EXPECT_EQ(formal_run[0]["inventory_id"].as<std::string>(), inventory_revision_id_);
    EXPECT_EQ(formal_run[0]["user_id"].as<std::string>(), confirmed_by_user_id_);
    EXPECT_EQ(formal_run[0]["input_checksum"].as<std::string>().substr(0, 7), "sha256:");
    EXPECT_EQ(formal_run[0]["rule_package_checksum"].as<std::string>().substr(0, 7), "sha256:");

    const auto year_result = client_->execSqlSync(
        "select status, is_current, overall_score, overall_grade from inspection_years where id = $1::uuid",
        outcome.inspection_year_id
    );
    ASSERT_EQ(year_result.size(), 1u);
    EXPECT_EQ(year_result[0]["status"].as<std::string>(), "已确认");
    EXPECT_TRUE(year_result[0]["is_current"].as<bool>());
    EXPECT_GE(year_result[0]["overall_score"].as<double>(), 0.0);
    EXPECT_LE(year_result[0]["overall_score"].as<double>(), 100.0);
    EXPECT_FALSE(year_result[0]["overall_grade"].as<std::string>().empty());

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
    EXPECT_EQ(
        validation_json["written"]["condition_ratings"].asInt(),
        outcome.written.condition_ratings);
    EXPECT_EQ(
        validation_json["written"]["assessment_run_id"].asString(),
        outcome.assessment_run_id);

    const auto component_result = client_->execSqlSync(
        "select id, structure_part, component_type, business_component_code, current_status, creation_source "
        "from bridge_components where bridge_id = $1::uuid",
        bridge_id_
    );
    ASSERT_EQ(component_result.size(), component_id_by_type_.size());
    EXPECT_EQ(
        observation_result[0]["bridge_component_id"].as<std::string>(),
        component_id_by_type_.at("h21.component.bearing"));
}

TEST_F(ConfirmAnnualFactsTest, ConfirmPersistsRangeMeasurementEndpoints) {
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto data = build_confirmed_data();
    auto& measurement = data["defects"][0]["measurements"][0];
    measurement["value_type"] = "range";
    measurement["value"] = Json::Value();
    measurement["minimum_value"] = 0.5;
    measurement["maximum_value"] = 4.0;
    measurement["is_approximate"] = true;
    measurement["source_text"] = "约0.5~4.0m";
    ASSERT_TRUE(repository.save_review_draft(import_record_id_, write_json_compact(data)));

    const auto outcome = repository.confirm_annual_facts(
        import_record_id_, false, "确认区间尺寸", confirmed_by_user_id_);
    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;

    const auto rows = client_->execSqlSync(
        "select dm.value_type, dm.numeric_value, dm.minimum_value, dm.maximum_value, dm.is_approximate, dm.raw_text "
        "from defect_measurements dm join defect_observations o on o.id = dm.defect_observation_id "
        "where o.source_import_record_id = $1::uuid and dm.measurement_type = '长度'",
        import_record_id_);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0]["value_type"].as<std::string>(), "range");
    EXPECT_TRUE(rows[0]["numeric_value"].isNull());
    EXPECT_DOUBLE_EQ(rows[0]["minimum_value"].as<double>(), 0.5);
    EXPECT_DOUBLE_EQ(rows[0]["maximum_value"].as<double>(), 4.0);
    EXPECT_TRUE(rows[0]["is_approximate"].as<bool>());
    EXPECT_EQ(rows[0]["raw_text"].as<std::string>(), "约0.5~4.0m");
}

TEST_F(ConfirmAnnualFactsTest, ConfirmStoresBlankDefectLocationAsNull) {
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto data = build_confirmed_data();
    data["defects"][0]["defect_location"] = "";
    ASSERT_TRUE(repository.save_review_draft(
        import_record_id_, write_json_compact(data)));

    const auto outcome = repository.confirm_annual_facts(
        import_record_id_, false, "允许来源未记录位置", confirmed_by_user_id_);
    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;

    const auto rows = client_->execSqlSync(
        "select defect_location from defect_observations "
        "where source_import_record_id = $1::uuid",
        import_record_id_);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0]["defect_location"].isNull());
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

    bridge_report::db::ReviewRepository repository(client_, registry_);
    const auto blocked_outcome =
        repository.confirm_annual_facts(
            import_record_id_, /*confirm_revision=*/false, "", confirmed_by_user_id_);

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
        repository.confirm_annual_facts(
            import_record_id_, /*confirm_revision=*/true, "修订确认", confirmed_by_user_id_);

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
    ASSERT_EQ(component_rating_after.size(), component_id_by_type_.size());
    for (const auto& row : component_rating_after) {
        EXPECT_EQ(row["inspection_year_id"].as<std::string>(),
                  confirmed_outcome.inspection_year_id);
    }

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

    bridge_report::db::ReviewRepository repository(client_, registry_);
    const auto outcome =
        repository.confirm_annual_facts(
            import_record_id_, /*confirm_revision=*/false, "", confirmed_by_user_id_);

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "import_record_wrong_status");

    const auto observation_count = client_->execSqlSync(
        "select count(*) as n from defect_observations where source_import_record_id = $1::uuid", import_record_id_
    );
    EXPECT_EQ(observation_count[0]["n"].as<int64_t>(), 0);
}

TEST_F(ConfirmAnnualFactsTest, confirm_rolls_back_on_failure) {
    bridge_report::db::ReviewRepository repository(client_, registry_);
    // 预置一个带合法系统运行来源的同年度同构件投影，令确认事务在写评分投影时
    // 触发唯一约束；失败发生在病害事实和评定明细写入之后，可验证整笔事务回滚。
    const auto package_row = client_->execSqlSync(
        "select content_checksum from standard_packages where id=$1::uuid", technical_package_id_);
    ASSERT_EQ(package_row.size(), 1u);
    const auto conflicting_run = client_->execSqlSync(
        "insert into assessment_runs(inspection_year_id,run_kind,formal_revision_number,"
        "technical_condition_package_id,standard_profile_id,component_inventory_revision_id,"
        "result_status,input_summary_json,input_checksum,rule_package_summary_json,rule_package_checksum,"
        "result_summary_json,created_by_user_id,confirmed_by_user_id,confirmed_at) "
        "values($1::uuid,'正式',99,$2::uuid,$3::uuid,$4::uuid,'成功','{\"source\":\"conflict-test\"}'::jsonb,"
        "$5,'{\"package\":\"conflict-test\"}'::jsonb,$6,'{}'::jsonb,$7::uuid,$7::uuid,now()) "
        "returning id::text as id",
        placeholder_year_id_, technical_package_id_, standard_profile_id_, inventory_revision_id_,
        "sha256:" + std::string(64, 'd'),
        package_row[0]["content_checksum"].as<std::string>(), confirmed_by_user_id_);
    client_->execSqlSync(
        "insert into condition_ratings(inspection_year_id,rating_level,structure_part,"
        "bridge_component_id,rating_item_name,score,review_status,assessment_run_id) "
        "values($1::uuid,'构件','上部结构',$2::uuid,'冲突占位',88,'已确认',$3::uuid)",
        placeholder_year_id_, component_id_by_type_.at("h21.component.bearing"),
        conflicting_run[0]["id"].as<std::string>());

    const auto outcome =
        repository.confirm_annual_facts(
            import_record_id_, /*confirm_revision=*/false, "", confirmed_by_user_id_);

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
    EXPECT_EQ(
        component_count[0]["n"].as<int64_t>(),
        static_cast<int64_t>(component_id_by_type_.size()));

    const auto assessment_count = client_->execSqlSync(
        "select count(*) as n from assessment_runs where source_import_record_id=$1::uuid",
        import_record_id_);
    EXPECT_EQ(assessment_count[0]["n"].as<int64_t>(), 0);

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

namespace {

// 保存校对草稿的事务化写入。复用 ConfirmAnnualFactsTest 的夹具：一座桥、已确认台账
// R1、锁定 R1 的待校对年度、已发布评定树、待校对导入记录，以及一份已绑定构件的草稿。
class SaveReviewDraftTest : public ConfirmAnnualFactsTest {
protected:
    bridge_report::db::SaveReviewDraftInput make_input(const Json::Value& draft) const {
        bridge_report::db::SaveReviewDraftInput input;
        input.import_record_id = import_record_id_;
        input.draft = draft;
        input.actor_username = "校对员";
        input.actor_is_admin = false;
        return input;
    }

    // 在桥上派生一条草稿台账版本（编号递增，status 默认即为“草稿”）。
    // 生产里改一个构件编号、加一条构件、设一次映射都会产生这样一行。
    std::string add_draft_revision() const {
        const auto rows = client_->execSqlSync(
            "insert into bridge_component_inventory_revisions"
            "(bridge_id,revision_number,baseline_revision_id,created_by_user_id) "
            "select $1::uuid,coalesce(max(revision_number),0)+1,$2::uuid,$3::uuid "
            "from bridge_component_inventory_revisions where bridge_id=$1::uuid "
            "returning id::text as id",
            bridge_id_, inventory_revision_id_, confirmed_by_user_id_);
        return rows[0]["id"].as<std::string>();
    }

    // 在桥上再确认一个更新的台账版本（复制 R1 的条目与映射，保证构件仍可解析）。
    std::string add_confirmed_revision(
        const std::string& deactivate_component_id = std::string()) const {
        const auto id = add_draft_revision();
        client_->execSqlSync(
            "insert into bridge_component_inventory_entries"
            "(inventory_revision_id,bridge_component_id,generation_batch_id,component_number,"
            "site_name,site_component_type,span_or_location,is_active,sort_order,remarks) "
            "select $1::uuid,bridge_component_id,generation_batch_id,component_number,site_name,"
            "site_component_type,span_or_location,is_active,sort_order,remarks "
            "from bridge_component_inventory_entries where inventory_revision_id=$2::uuid",
            id, inventory_revision_id_);
        client_->execSqlSync(
            "insert into bridge_component_standard_mappings(inventory_entry_id,standard_package_id,"
            "standard_bridge_type_id,standard_component_category_id,structure_part,mapping_source,"
            "confirmation_status,confirmed_by_user_id,confirmed_at) "
            "select new_entry.id,m.standard_package_id,m.standard_bridge_type_id,"
            "m.standard_component_category_id,m.structure_part,m.mapping_source,"
            "m.confirmation_status,m.confirmed_by_user_id,m.confirmed_at "
            "from bridge_component_standard_mappings m "
            "join bridge_component_inventory_entries old_entry on old_entry.id=m.inventory_entry_id "
            "  and old_entry.inventory_revision_id=$2::uuid "
            "join bridge_component_inventory_entries new_entry "
            "  on new_entry.inventory_revision_id=$1::uuid "
            "  and new_entry.bridge_component_id=old_entry.bridge_component_id",
            id, inventory_revision_id_);
        if (!deactivate_component_id.empty()) {
            // 必须趁版本还是草稿时改：已确认版本的条目由触发器保护为不可变。
            client_->execSqlSync(
                "update bridge_component_inventory_entries set is_active=false,"
                "deactivated_at=now(),deactivation_reason='测试停用' "
                "where inventory_revision_id=$1::uuid and bridge_component_id=$2::uuid",
                id, deactivate_component_id);
        }
        client_->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认',"
            "confirmed_by_user_id=$2::uuid,confirmed_at=now(),confirmation_note='测试确认' "
            "where id=$1::uuid",
            id, confirmed_by_user_id_);
        return id;
    }

    void unlock_year_revision() const {
        client_->execSqlSync(
            "update inspection_years set component_inventory_revision_id=null where id=$1::uuid",
            placeholder_year_id_);
    }

    std::optional<std::string> year_locked_revision_id() const {
        const auto rows = client_->execSqlSync(
            "select component_inventory_revision_id::text as id from inspection_years "
            "where id=$1::uuid",
            placeholder_year_id_);
        if (rows.empty() || rows[0]["id"].isNull()) return std::nullopt;
        return rows[0]["id"].as<std::string>();
    }

    std::string stored_parsed_result_json() const {
        return client_->execSqlSync(
            "select parsed_result_json::text as json from import_records where id=$1::uuid",
            import_record_id_)[0]["json"].as<std::string>();
    }
};

}  // 匿名命名空间

// 缺陷本体：桥上一有台账草稿，草稿优先的解析就取到草稿版本，每条已绑定病害都被判成
// “台账已变化”，整份校对草稿存不了盘。把解析换回草稿优先的排序，这条必红。
TEST_F(SaveReviewDraftTest, SavesBoundDefectsWhileTheBridgeHasADraftRevision) {
    add_draft_revision();
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto data = build_confirmed_data();
    data["defects"][0]["defect_description"] = "校对期改了一句描述";

    const auto outcome = repository.save_review_draft(make_input(data));

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    EXPECT_TRUE(outcome.validation.issues.empty());
    EXPECT_NE(stored_parsed_result_json().find("校对期改了一句描述"), std::string::npos);
}

// 年度锁定优先：桥上后来又确认了 R2，但本年度锁的是 R1，就必须继续按 R1 校验。
TEST_F(SaveReviewDraftTest, KeepsTheYearLockedRevisionEvenWhenANewerConfirmedOneExists) {
    const auto newer_revision_id = add_confirmed_revision();
    ASSERT_NE(newer_revision_id, inventory_revision_id_);
    bridge_report::db::ReviewRepository repository(client_, registry_);

    const auto outcome = repository.save_review_draft(make_input(build_confirmed_data()));

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    EXPECT_EQ(year_locked_revision_id().value_or(""), inventory_revision_id_);
}

// 含构件绑定的草稿保存成功后要锁定年度版本，否则草稿按 R1 存下、年度仍未锁定，
// 别人确认 R2 之后下次加载就解析成 R2，刚存的绑定立刻变成旧版本数据。
TEST_F(SaveReviewDraftTest, LocksTheResolvedRevisionIntoTheYearWhenTheDraftBindsComponents) {
    unlock_year_revision();
    ASSERT_FALSE(year_locked_revision_id().has_value());
    bridge_report::db::ReviewRepository repository(client_, registry_);

    const auto outcome = repository.save_review_draft(make_input(build_confirmed_data()));

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    EXPECT_EQ(year_locked_revision_id().value_or(""), inventory_revision_id_);
}

// 纯文本编辑没有理由给年度定版本。
TEST_F(SaveReviewDraftTest, LeavesTheYearUnlockedWhenTheDraftBindsNoComponent) {
    unlock_year_revision();
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto data = build_confirmed_data();
    for (auto& defect : data["defects"]) {
        defect["bridge_component_id"] = Json::Value();
        defect["standard_component_category_id"] = Json::Value();
        defect["resolved_structure_part"] = Json::Value();
        defect["component_inventory_revision_id"] = Json::Value();
    }

    const auto outcome = repository.save_review_draft(make_input(data));

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    EXPECT_FALSE(year_locked_revision_id().has_value());
}

// 整份草稿一致地引用旧版本 -> 整体一条 409，不逐条“请重新选择”。
TEST_F(SaveReviewDraftTest, ReportsAStaleRevisionOnceForTheWholeRequest) {
    unlock_year_revision();
    const auto newer_revision_id = add_confirmed_revision();
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto data = build_confirmed_data();  // 病害仍写着 R1，服务端将解析出 R2

    const auto outcome = repository.save_review_draft(make_input(data));

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "component_inventory_revision_changed");
    EXPECT_TRUE(outcome.validation.issues.empty()) << "版本冲突不该退化成逐项问题";
    EXPECT_FALSE(outcome.error_message.empty());
    // 失败即整体回滚：草稿没写、年度也没被锁上。
    EXPECT_FALSE(year_locked_revision_id().has_value());
    EXPECT_EQ(stored_parsed_result_json().find("校对期改了一句描述"), std::string::npos);
    (void)newer_revision_id;
}

// 请求内部混用多个版本属于草稿数据非法，仍旧逐项返回。
TEST_F(SaveReviewDraftTest, ReportsPerDefectIssuesWhenTheRequestMixesRevisions) {
    const auto other_revision_id = add_confirmed_revision();
    client_->execSqlSync(
        "update inspection_years set component_inventory_revision_id=$2::uuid where id=$1::uuid",
        placeholder_year_id_, inventory_revision_id_);
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto data = build_confirmed_data();
    auto mixed = data["defects"][0];
    mixed["candidate_id"] = "defect_mixed_revision";
    mixed["component_inventory_revision_id"] = other_revision_id;
    data["defects"].append(mixed);

    const auto outcome = repository.save_review_draft(make_input(data));

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "defect_component_assignment_invalid");
    EXPECT_EQ(outcome.validation.code, "defect_component_assignment_invalid");
    ASSERT_FALSE(outcome.validation.issues.empty()) << "混用版本必须给出逐项 details";
}

// 版本对得上、但构件在该版本里已停用 -> 逐项 details，不是整体版本冲突。
TEST_F(SaveReviewDraftTest, ReportsPerDefectIssuesWhenTheComponentIsDeactivated) {
    auto data = build_confirmed_data();
    const auto component_id = data["defects"][0]["bridge_component_id"].asString();
    const auto revision_with_deactivated = add_confirmed_revision(component_id);
    client_->execSqlSync(
        "update inspection_years set component_inventory_revision_id=$2::uuid where id=$1::uuid",
        placeholder_year_id_, revision_with_deactivated);
    for (auto& defect : data["defects"]) {
        if (!defect["bridge_component_id"].isString()) continue;
        if (defect["bridge_component_id"].asString().empty()) continue;
        defect["component_inventory_revision_id"] = revision_with_deactivated;
    }
    bridge_report::db::ReviewRepository repository(client_, registry_);

    const auto outcome = repository.save_review_draft(make_input(data));

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "defect_component_assignment_invalid");
    ASSERT_FALSE(outcome.validation.issues.empty());
}

// 版本对得上、但提交的规范类别与该版本的映射对不上 -> 同样是逐项 details。
TEST_F(SaveReviewDraftTest, ReportsPerDefectIssuesWhenTheCategoryDoesNotMatchTheMapping) {
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto data = build_confirmed_data();
    data["defects"][0]["standard_component_category_id"] = "h21.component.deck_pavement";

    const auto outcome = repository.save_review_draft(make_input(data));

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "defect_component_assignment_invalid");
    ASSERT_FALSE(outcome.validation.issues.empty());
}

// 年度锁在草稿版本上（待校对年度允许这么锁）：解析不出可用的已确认版本。
// 这是年度上下文错误，不能伪装成每条病害各自的数据错误。
TEST_F(SaveReviewDraftTest, ReportsAContextErrorWhenTheYearIsLockedToADraftRevision) {
    const auto draft_revision_id = add_draft_revision();
    client_->execSqlSync(
        "update inspection_years set component_inventory_revision_id=$2::uuid where id=$1::uuid",
        placeholder_year_id_, draft_revision_id);
    bridge_report::db::ReviewRepository repository(client_, registry_);

    const auto outcome = repository.save_review_draft(make_input(build_confirmed_data()));

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "component_inventory_unavailable");
    EXPECT_TRUE(outcome.validation.issues.empty());
}

// 编辑锁失效与状态变化必须能与版本冲突、校验失败分辨开——改造前它们全被压成
// 同一个 409 import_record_not_editable。
TEST_F(SaveReviewDraftTest, DistinguishesAnInvalidEditLockFromOtherFailures) {
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto input = make_input(build_confirmed_data());
    input.edit_lock = bridge_report::db::EditLockCredentials{
        confirmed_by_user_id_, confirmed_by_user_id_, "never-issued-token"};

    const auto outcome = repository.save_review_draft(input);

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "edit_lock_invalid");
    EXPECT_TRUE(outcome.validation.issues.empty());
}

TEST_F(SaveReviewDraftTest, DistinguishesANonEditableImportRecord) {
    client_->execSqlSync(
        "update import_records set import_status='已取消' where id=$1::uuid", import_record_id_);
    bridge_report::db::ReviewRepository repository(client_, registry_);

    const auto outcome = repository.save_review_draft(make_input(build_confirmed_data()));

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "import_record_not_editable");
}

// 规范组合与评定树在事务内重新读取，路由不再传任何一项进来。
TEST_F(SaveReviewDraftTest, ReadsTheStandardProfileInsideTheTransaction) {
    client_->execSqlSync(
        "update inspection_years set standard_profile_id=null where id=$1::uuid",
        placeholder_year_id_);
    bridge_report::db::ReviewRepository repository(client_, registry_);

    const auto outcome = repository.save_review_draft(make_input(build_confirmed_data()));

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "rating_tree_required");
}

// 事务内抛出的数据库异常统一落到 db_write_failed（500），与提交回调失败的
// database_commit_failed 分开返回。
TEST_F(SaveReviewDraftTest, ReportsDbWriteFailedWhenTheStatementThrows) {
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto input = make_input(build_confirmed_data());
    input.import_record_id = "not-a-uuid";

    const auto outcome = repository.save_review_draft(input);

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "db_write_failed");
    EXPECT_FALSE(outcome.error_message.empty());
}

// 年度锁定发生在构件关联校验通过之后、写入之前。评定树校验在它之后失败时，
// 这次锁定必须随事务一起回滚，不能留下"版本锁死了、草稿却没存"的年度。
TEST_F(SaveReviewDraftTest, RollsBackTheYearLockWhenALaterStepFails) {
    unlock_year_revision();
    ASSERT_FALSE(year_locked_revision_id().has_value());
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto data = build_confirmed_data();
    // 构件关联合法（锁定会真的执行），但节点不在本年度的评定树里。
    data["defects"][0]["rating_tree_node_id"] = "11111111-1111-4111-8111-111111111111";
    data["defects"][0]["rating_tree_match_method"] = "manual";

    const auto outcome = repository.save_review_draft(make_input(data));

    ASSERT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "defect_rating_tree_assignment_invalid");
    EXPECT_FALSE(year_locked_revision_id().has_value()) << "后续步骤失败时年度锁定必须一起回滚";
    EXPECT_EQ(stored_parsed_result_json().find("11111111-1111-4111-8111-111111111111"),
              std::string::npos);
}

// ConfirmLocksTheResolvedRevisionBeforePreflight 的失败面：版本锁定发生在
// build_preflight_report() 之前，所以 preflight 挡回去时那次锁定必须一起回滚。
// 否则一次失败的确认会给年度留下一个它自己并没有确认过的台账版本，
// 而后续按"年度锁定优先"解析的每一处都会认这个版本。
TEST_F(ConfirmAnnualFactsTest, RollsBackTheYearRevisionLockWhenPreflightBlocks) {
    client_->execSqlSync(
        "update inspection_years set component_inventory_revision_id=null where id=$1::uuid",
        placeholder_year_id_);

    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto data = build_confirmed_data();
    // 病害已校对、契约合法，但没绑实际构件：preflight 的 check_component_inventory_links
    // 会以 defect_component_match_required 阻断。挑这个而不是"未校对"，是因为后者挂在
    // **契约校验**上，而契约校验发生在版本锁定之前，测不到要守的那段。
    data["defects"][0]["bridge_component_id"] = Json::Value();
    data["defects"][0]["standard_component_category_id"] = Json::Value();
    data["defects"][0]["resolved_structure_part"] = Json::Value();
    data["defects"][0]["component_inventory_revision_id"] = Json::Value();
    ASSERT_TRUE(repository.save_review_draft(import_record_id_, write_json_compact(data)));

    const auto outcome = repository.confirm_annual_facts(
        import_record_id_, false, "preflight 应当挡下", confirmed_by_user_id_);

    ASSERT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "preflight_failed");
    // 必须是入库前检查挡的，不能是契约校验——后者发生在版本锁定**之前**，
    // 那样这条测试就绕开了它要守的那段。
    EXPECT_EQ(outcome.error_message, "最新草稿未通过入库前检查。");
    const auto year = client_->execSqlSync(
        "select component_inventory_revision_id::text as revision_id, status "
        "from inspection_years where id=$1::uuid", placeholder_year_id_);
    EXPECT_TRUE(year[0]["revision_id"].isNull())
        << "确认失败时事务内的版本锁定必须一并回滚";
    EXPECT_EQ(year[0]["status"].as<std::string>(), "待校对");
}

// override 与年度锁定版本**相同**时是正常路径，不能因为"传了 override"就报冲突。
// ExplicitRevisionIsValidatedAndCannotOverrideALockedYear 只覆盖了"不同"那一半；
// 少了这一条，把冲突判定写成"只要传了 override 就报错"也照样绿。
TEST_F(ConfirmAnnualFactsTest, ExplicitRevisionMatchingTheLockedYearIsAccepted) {
    bridge_report::assessment::AssessmentConfirmationService service(client_, registry_);

    const auto outcome = service.calculate(
        placeholder_year_id_, build_confirmed_data(),
        std::optional<std::string>(inventory_revision_id_));

    for (const auto& item : outcome.preview.issues) {
        EXPECT_NE(item.code, "component_inventory_revision_changed")
            << "override 与年度锁定版本相同不是冲突：" << item.message;
        EXPECT_NE(item.code, "assessment_context_incomplete") << item.message;
    }
    EXPECT_EQ(outcome.status, bridge_report::assessment::AssessmentConfirmationStatus::Completed);
}

// override 不是绕过校验的后门：草稿版本与不存在的版本都必须被拒。
// 前者尤其要紧——待校对年度可以合法地锁在草稿上，若 override 放行草稿，
// 评定就会按一份还在改的台账算出正式结论。
TEST_F(ConfirmAnnualFactsTest, ExplicitRevisionRejectsADraftOrUnknownRevision) {
    client_->execSqlSync(
        "update inspection_years set component_inventory_revision_id=null where id=$1::uuid",
        placeholder_year_id_);
    const auto draft_revision = client_->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
        "baseline_revision_id,created_by_user_id) values($1::uuid,2,$2::uuid,$3::uuid) "
        "returning id::text as id",
        bridge_id_, inventory_revision_id_, confirmed_by_user_id_)[0]["id"].as<std::string>();
    bridge_report::assessment::AssessmentConfirmationService service(client_, registry_);

    const auto with_draft = service.calculate(
        placeholder_year_id_, build_confirmed_data(),
        std::optional<std::string>(draft_revision));
    bool draft_rejected = false;
    for (const auto& item : with_draft.preview.issues) {
        if (item.code == "assessment_context_incomplete") draft_rejected = true;
    }
    EXPECT_TRUE(draft_rejected) << "草稿台账版本不得作为评定输入";

    const auto with_unknown = service.calculate(
        placeholder_year_id_, build_confirmed_data(),
        std::optional<std::string>("00000000-0000-0000-0000-000000000000"));
    bool unknown_rejected = false;
    for (const auto& item : with_unknown.preview.issues) {
        if (item.code == "assessment_context_incomplete") unknown_rejected = true;
    }
    EXPECT_TRUE(unknown_rejected) << "不存在的台账版本不得作为评定输入";

    // 两次拒绝都不得顺手把年度锁上。
    EXPECT_TRUE(client_->execSqlSync(
        "select component_inventory_revision_id::text as revision_id from inspection_years "
        "where id=$1::uuid", placeholder_year_id_)[0]["revision_id"].isNull());
}

// 确认事务必须在**读年度数据之前**就拿到年度行锁。7f89294 把版本解析与锁定提到了
// build_preflight_report() 之前，但那条 for update 本身一直没有测试——
// 3b8c43b 就是在同一类位置丢过一次行锁（见 80272ab），只靠注释守不住。
//
// 判据靠一个能区分的出口做成确定的：让草稿在**预检**阶段失败。
//   - 有行锁：别人握着年度行时，事务卡在第 2 步的 select ... for update 上，
//     语句超时 -> db_write_failed，根本走不到预检；
//   - 没行锁：那句 select 照常返回，一路走到预检才失败 -> preflight_failed，全程不阻塞。
// 两个错误码不同，把 for update 去掉这条必红。
TEST_F(ConfirmAnnualFactsTest, ConfirmTakesTheYearRowLockBeforeReadingTheYear) {
    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto data = build_confirmed_data();
    // 契约合法但没绑实际构件：preflight 的 check_component_inventory_links 会阻断。
    // 挑它是因为"未校对"那种挂在契约校验上，而契约校验发生在年度行锁之前，
    // 两条路都走不到要守的那句。
    data["defects"][0]["bridge_component_id"] = Json::Value();
    data["defects"][0]["standard_component_category_id"] = Json::Value();
    data["defects"][0]["resolved_structure_part"] = Json::Value();
    data["defects"][0]["component_inventory_revision_id"] = Json::Value();
    ASSERT_TRUE(repository.save_review_draft(import_record_id_, write_json_compact(data)));

    // 阻塞方另开一条连接：client_ 的连接池只有 1 条，用它开事务会把仓储饿死。
    auto blocker_client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);
    auto blocker = blocker_client->newTransaction();
    blocker->execSqlSync(
        "select id from inspection_years where id=$1::uuid for update", placeholder_year_id_);

    client_->execSqlSync("set statement_timeout = '1500'");
    const auto outcome = repository.confirm_annual_facts(
        import_record_id_, false, "别人握着年度行", confirmed_by_user_id_);
    client_->execSqlSync("set statement_timeout = 0");

    blocker->rollback();
    blocker_client->closeAll();

    ASSERT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "db_write_failed")
        << "别人握着年度行时，确认事务必须卡在年度行锁上，而不是照常读下去";
    EXPECT_NE(outcome.error_code, "preflight_failed")
        << "走到了预检说明年度数据是在没有行锁的情况下读的";

    // 卡住即整体回滚：导入记录仍待校对，年度没被动过。
    const auto after = client_->execSqlSync(
        "select ir.import_status, iy.status as year_status "
        "from import_records ir join inspection_years iy on iy.id=ir.inspection_year_id "
        "where ir.id=$1::uuid", import_record_id_);
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after[0]["import_status"].as<std::string>(), "待校对");
    EXPECT_EQ(after[0]["year_status"].as<std::string>(), "待校对");
}

// 两条导入记录可以挂在同一个年度上（import_records.inspection_year_id 没有唯一约束）。
// 其中一条正在确认、已经把年度定到 R1 但还没提交时，另一条不能按自己解析出的版本
// 一路走下去——它必须等前者落定，然后采用前者的结果。
//
// 造一个真会分叉的局面：桥上有 R1 与 R2 两个已确认版本，年度未锁定。
// 自己解析的话会取"最新已确认"R2；而先手正在把年度定到 R1。
//   - 在年度行锁内读：卡住 -> 先手提交 -> 读到 R1 -> 按 R1 确认，年度最终是 R1；
//   - 不在行锁内读：读到的是先手提交前的 null -> 解析成 R2 -> 随后
//     lock_pending_year_revision 回读发现年度已是 R1，判定抢锁失败，整笔确认告吹。
// 成功/失败两分，判据确定。
TEST_F(ConfirmAnnualFactsTest, ASecondConfirmOnTheSameYearAdoptsTheSettledRevision) {
    // 桥上再确认一个更新的版本，并复制条目与映射，保证它自身可解析可用。
    const auto newer_revision = client_->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
        "baseline_revision_id,created_by_user_id) values($1::uuid,2,$2::uuid,$3::uuid) "
        "returning id::text as id",
        bridge_id_, inventory_revision_id_, confirmed_by_user_id_)[0]["id"].as<std::string>();
    client_->execSqlSync(
        "insert into bridge_component_inventory_entries(inventory_revision_id,bridge_component_id,"
        "component_number,site_name,site_component_type,sort_order) "
        "select $1::uuid,bridge_component_id,component_number,site_name,site_component_type,sort_order "
        "from bridge_component_inventory_entries where inventory_revision_id=$2::uuid",
        newer_revision, inventory_revision_id_);
    client_->execSqlSync(
        "insert into bridge_component_standard_mappings(inventory_entry_id,standard_package_id,"
        "standard_bridge_type_id,standard_component_category_id,structure_part,mapping_source,"
        "confirmation_status,confirmed_by_user_id,confirmed_at) "
        "select e.id,m.standard_package_id,m.standard_bridge_type_id,m.standard_component_category_id,"
        "m.structure_part,m.mapping_source,m.confirmation_status,m.confirmed_by_user_id,m.confirmed_at "
        "from bridge_component_standard_mappings m "
        "join bridge_component_inventory_entries o on o.id=m.inventory_entry_id "
        "  and o.inventory_revision_id=$2::uuid "
        "join bridge_component_inventory_entries e on e.inventory_revision_id=$1::uuid "
        "  and e.bridge_component_id=o.bridge_component_id",
        newer_revision, inventory_revision_id_);
    client_->execSqlSync(
        "update bridge_component_inventory_revisions set status='已确认',"
        "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
        newer_revision, confirmed_by_user_id_);
    client_->execSqlSync(
        "update inspection_years set component_inventory_revision_id=null where id=$1::uuid",
        placeholder_year_id_);

    bridge_report::db::ReviewRepository seeder(client_, registry_);
    ASSERT_TRUE(seeder.save_review_draft(
        import_record_id_, write_json_compact(build_confirmed_data())));

    // 先手：把年度定到 R1 但不提交，握住年度行。
    auto blocker_client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);
    auto blocker = blocker_client->newTransaction();
    blocker->execSqlSync(
        "update inspection_years set component_inventory_revision_id=$2::uuid where id=$1::uuid",
        placeholder_year_id_, inventory_revision_id_);

    auto confirm_client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);
    bridge_report::db::ConfirmOutcome outcome;
    std::thread confirmer([&] {
        outcome = bridge_report::db::ReviewRepository(confirm_client, registry_)
                      .confirm_annual_facts(
                          import_record_id_, false, "共享年度并发确认", confirmed_by_user_id_);
    });

    // 等到确认线程确实被年度行挡住，再放行先手——不用固定 sleep。
    auto probe_client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);
    bool confirm_blocked = false;
    for (int attempt = 0; attempt < 250 && !confirm_blocked; ++attempt) {
        confirm_blocked = probe_client->execSqlSync(
            "select count(*) as n from pg_stat_activity "
            "where wait_event_type='Lock' and datname=current_database()"
        )[0]["n"].as<int>() > 0;
        if (!confirm_blocked) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // 只用来确认两个事务真的相遇了（否则后面的断言测不到东西）。它不区分卡在哪句：
    // 去掉年度行锁后，确认会改为卡在 lock_pending_year_revision 的 UPDATE 上，同样为真。
    // 真正的判据是下面的 outcome.success 与年度最终版本。
    ASSERT_TRUE(confirm_blocked) << "两个事务没有相遇，本条的前提不成立";

    blocker->execSqlSync("commit");
    confirmer.join();
    blocker_client->closeAll();
    confirm_client->closeAll();
    probe_client->closeAll();

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    const auto year = client_->execSqlSync(
        "select component_inventory_revision_id::text as revision_id from inspection_years "
        "where id=$1::uuid", outcome.inspection_year_id);
    ASSERT_FALSE(year[0]["revision_id"].isNull());
    EXPECT_EQ(year[0]["revision_id"].as<std::string>(), inventory_revision_id_)
        << "必须采用先手落定的 R1，而不是自己解析出的 R2";
    EXPECT_NE(year[0]["revision_id"].as<std::string>(), newer_revision);
}

// database_commit_failed 是一条真实但极难触发的分支：所有语句都成功了，事务却在
// COMMIT 那一刻失败。CommitLatch 自己有单元测试，但"真实的提交失败能不能流到这个
// 错误码"从来没验过——中间隔着 drogon 的提交回调。
//
// 造法是延迟约束触发器：CONSTRAINT TRIGGER ... DEFERRABLE INITIALLY DEFERRED 在
// COMMIT 时才执行，在里面 raise，COMMIT 就会失败，而此前每一句都是成功的。这是能
// 精确制造"语句全成、提交失败"的少数手段之一。
TEST_F(SaveReviewDraftTest, ReportsDatabaseCommitFailedWhenTheCommitItselfFails) {
    client_->execSqlSync(
        "create or replace function bridge_report_test_fail_commit() returns trigger as $$ "
        "begin raise exception 'forced commit failure'; end; $$ language plpgsql");
    client_->execSqlSync(
        "create constraint trigger trg_bridge_report_test_fail_commit "
        "after update on import_records deferrable initially deferred "
        "for each row execute function bridge_report_test_fail_commit()");

    bridge_report::db::ReviewRepository repository(client_, registry_);
    auto data = build_confirmed_data();
    // 夹具在 SetUp 里已经把同一份数据写进库了，得带一个独有标记才能验证"没落盘"。
    data["defects"][0]["defect_description"] = "提交失败不得落盘的标记";
    const auto outcome = repository.save_review_draft(make_input(data));

    // 先拆掉触发器再断言：断言失败会抛，留着它会污染后面所有用到 import_records 的测试。
    client_->execSqlSync("drop trigger trg_bridge_report_test_fail_commit on import_records");
    client_->execSqlSync("drop function bridge_report_test_fail_commit()");

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "database_commit_failed")
        << "提交阶段失败必须与语句阶段的 db_write_failed 分开报，"
           "两者的排查方向完全不同；实际拿到：" << outcome.error_message;
    EXPECT_TRUE(outcome.validation.issues.empty());

    // 提交失败等于什么都没发生：草稿不得落盘。
    EXPECT_EQ(stored_parsed_result_json().find("提交失败不得落盘的标记"), std::string::npos);
}

// 与上一条同一手法，守的是正式入库那条路。confirm_annual_facts 一次事务写进
// defect_observations / condition_ratings / assessment_* 等多张事实表，提交失败若被
// 当成成功，用户会看到"入库成功"而库里什么都没有——比草稿保存的后果重得多。
TEST_F(ConfirmAnnualFactsTest, ReportsDatabaseCommitFailedWhenTheCommitItselfFails) {
    bridge_report::db::ReviewRepository repository(client_, registry_);
    ASSERT_TRUE(repository.save_review_draft(
        import_record_id_, write_json_compact(build_confirmed_data())));
    client_->execSqlSync(
        "create or replace function bridge_report_test_fail_confirm_commit() returns trigger as $$ "
        "begin raise exception 'forced commit failure'; end; $$ language plpgsql");
    client_->execSqlSync(
        "create constraint trigger trg_bridge_report_test_fail_confirm_commit "
        "after update on inspection_years deferrable initially deferred "
        "for each row execute function bridge_report_test_fail_confirm_commit()");

    const auto outcome = repository.confirm_annual_facts(
        import_record_id_, false, "提交阶段失败", confirmed_by_user_id_);

    client_->execSqlSync(
        "drop trigger trg_bridge_report_test_fail_confirm_commit on inspection_years");
    client_->execSqlSync("drop function bridge_report_test_fail_confirm_commit()");

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "database_commit_failed")
        << "实际拿到：" << outcome.error_message;

    // 提交失败等于什么都没发生：事实表为空，导入记录仍待校对。
    EXPECT_TRUE(client_->execSqlSync(
        "select 1 from defect_observations where source_import_record_id=$1::uuid",
        import_record_id_).empty());
    EXPECT_EQ(client_->execSqlSync(
        "select import_status from import_records where id=$1::uuid",
        import_record_id_)[0]["import_status"].as<std::string>(), "待校对");
}
