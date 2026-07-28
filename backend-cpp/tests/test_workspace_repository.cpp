#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/auth/PasswordHash.hpp"
#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/archive/WordInputArchive.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ReviewRepository.hpp"
#include "bridge_report/db/WorkspaceRepository.hpp"

namespace {

class WorkspaceRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        const bridge_report::config::PostgresConfig config{};
        client_ = bridge_report::db::create_db_client(config, 1);

        bridge_id_ = insert_id(
            "insert into bridges (bridge_name, route_name) values ('M065工作区测试桥', 'G305') returning id");
        standard_user_id_ = insert_id(
            "insert into users (username, display_name, password_hash, role) "
            "values ($1, '工作区规范测试员', 'not-a-real-hash', 'normal') returning id",
            "workspace_standard_" + bridge_id_);
        technical_package_id_ = insert_id(
            "insert into standard_packages (standard_family, standard_id, standard_code, standard_name, "
            "official_edition, package_version, contract_version, algorithm_id, effective_date, content_checksum) "
            "values ('technical_condition', $1, 'TEST H21', '测试技术标准', '2026', '1.0.0', 1, "
            "'test-h21', '2026-01-01', $2) returning id",
            "WORKSPACE-TECH-" + bridge_id_, "sha256:" + std::string(64, '6'));
        maintenance_package_id_ = insert_id(
            "insert into standard_packages (standard_family, standard_id, standard_code, standard_name, "
            "official_edition, package_version, contract_version, algorithm_id, effective_date, content_checksum) "
            "values ('maintenance', $1, 'TEST 5120', '测试养护规范', '2026', '1.0.0', 1, "
            "'test-maintenance', '2026-01-01', $2) returning id",
            "WORKSPACE-MAINT-" + bridge_id_, "sha256:" + std::string(64, '7'));
        rating_tree_version_id_ = insert_id(
            "insert into rating_tree_versions ("
            "tree_code,tree_name,package_version,contract_version,"
            "technical_condition_package_id,technical_condition_standard_id,"
            "technical_condition_package_version,technical_condition_content_checksum,"
            "maintenance_package_id,maintenance_standard_id,"
            "maintenance_package_version,maintenance_content_checksum,"
            "organization_tree_code,organization_package_version,"
            "organization_content_checksum,tree_content_checksum,status"
            ") values ($1,'工作区测试评定树','1.0.0',1,$2::uuid,$3,'1.0.0',$4,"
            "$5::uuid,$6,'1.0.0',$7,$1,'1.0.0',$8,$9,'draft') returning id",
            "WORKSPACE-TREE-" + bridge_id_,
            technical_package_id_,
            "WORKSPACE-TECH-" + bridge_id_,
            "sha256:" + std::string(64, '6'),
            maintenance_package_id_,
            "WORKSPACE-MAINT-" + bridge_id_,
            "sha256:" + std::string(64, '7'),
            "sha256:" + bridge_report::auth::sha256_hex(bridge_id_ + "-org"),
            "sha256:" + bridge_report::auth::sha256_hex(bridge_id_ + "-tree"));
        insert_id(
            "insert into rating_tree_nodes ("
            "rating_tree_version_id,node_key,display_name,node_type,scoring_mode"
            ") values ($1::uuid,'root','桥梁评定','root','non_scoring') returning id",
            rating_tree_version_id_);
        client_->execSqlSync(
            "update rating_tree_versions set status='published',published_at=now() "
            "where id=$1::uuid",
            rating_tree_version_id_);
        standard_profile_id_ = insert_id(
            "insert into project_standard_profiles (technical_condition_package_id, maintenance_package_id, "
            "rating_tree_version_id,created_by_user_id, change_reason) "
            "values ($1::uuid, $2::uuid, $3::uuid,$4::uuid, '工作区系统评定测试') "
            "returning id",
            technical_package_id_, maintenance_package_id_,
            rating_tree_version_id_, standard_user_id_);
        inventory_revision_id_ = insert_id(
            "insert into bridge_component_inventory_revisions (bridge_id, revision_number, status, "
            "created_by_user_id, confirmed_by_user_id, confirmed_at, confirmation_note) "
            "values ($1::uuid, 1, '已确认', $2::uuid, $2::uuid, now(), '工作区系统评定测试') returning id",
            bridge_id_, standard_user_id_);
        upload_root_ = std::filesystem::temp_directory_path() / ("bridge-report-upload-" + bridge_id_);
        std::filesystem::remove_all(upload_root_);

        confirmed_year_id_ = insert_id(
            "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current, "
            "overall_score, overall_grade, standard_profile_id, component_inventory_revision_id) "
            "values ($1::uuid, 2025, '已确认', 1, true, 85.61, '2类', $2::uuid, $3::uuid) returning id",
            bridge_id_, standard_profile_id_, inventory_revision_id_);
        superseded_year_id_ = insert_id(
            "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current, "
            "overall_score, overall_grade) values ($1::uuid, 2026, '已被修订', 1, false, 99.99, '1类') returning id",
            bridge_id_);
        pending_year_id_ = insert_id(
            "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current) "
            "values ($1::uuid, 2027, '待校对', 1, true) returning id", bridge_id_);

        assessment_run_id_ = insert_id(
            "insert into assessment_runs (inspection_year_id, run_kind, result_status, input_summary_json, "
            "input_checksum, rule_package_summary_json, rule_package_checksum, result_summary_json, "
            "created_by_user_id, formal_revision_number, technical_condition_package_id, "
            "standard_profile_id, component_inventory_revision_id, is_current, confirmed_by_user_id, confirmed_at) "
            "values ($1::uuid, '正式', '成功', '{\"source\":\"workspace-test\"}'::jsonb, "
            "$2, '{\"package\":\"workspace-test\"}'::jsonb, $3, '{\"score\":85.61}'::jsonb, $4::uuid, "
            "1, $5::uuid, $6::uuid, $7::uuid, true, $4::uuid, now()) "
            "returning id",
            confirmed_year_id_, "sha256:" + std::string(64, '8'),
            "sha256:" + std::string(64, '6'), standard_user_id_, technical_package_id_,
            standard_profile_id_, inventory_revision_id_);

        client_->execSqlSync(
            "insert into condition_ratings (inspection_year_id, rating_level, structure_part, rating_item_name, "
            "score, grade, review_status, assessment_run_id) values "
            "($1::uuid, '结构分部', '上部结构', '上部结构', 87.45, '2类', '已确认', $2::uuid)",
            confirmed_year_id_, assessment_run_id_);

        component_id_ = insert_id(
            "insert into bridge_components (bridge_id, structure_part, component_type, business_component_code, "
            "normalized_component_key) values ($1::uuid, '上部结构', '空心板', '2-1#板', "
            "'上部结构|空心板|2-1#板') returning id", bridge_id_);
        thread_id_ = insert_id(
            "insert into defect_threads (bridge_id, bridge_component_id, thread_name, defect_type, defect_location, "
            "first_seen_inspection_id, latest_seen_inspection_id, confirmation_status) "
            "values ($1::uuid, $2::uuid, '蜂窝、麻面｜左侧端部', '蜂窝、麻面', '左侧端部', "
            "$3::uuid, $3::uuid, '人工已确认') returning id", bridge_id_, component_id_, confirmed_year_id_);
        bound_observation_id_ = insert_observation(thread_id_, "蜂窝、麻面");
        unbound_observation_id_ = insert_observation(std::nullopt, "横向裂缝");

        const std::string parsed =
            R"({"defects":[{"review_status":"待确认","warnings":[]}],"photos":[],)"
            R"("ratings":{"overall":{"review_status":"已确认","warnings":[]},)"
            R"("structure_parts":[],"evaluation_parts":[],"component_ratings":[]}})";
        pending_import_id_ = insert_id(
            "insert into import_records (bridge_id, inspection_year_id, import_name, source_type, import_status, "
            "parsed_result_json) values ($1::uuid, $2::uuid, '2027报告.docx', '软件导出Word', '待校对', $3::jsonb) "
            "returning id", bridge_id_, pending_year_id_, parsed);
        confirmed_import_id_ = insert_id(
            "insert into import_records (bridge_id, inspection_year_id, import_name, source_type, import_status) "
            "values ($1::uuid, $2::uuid, '2025报告.docx', '正式Word', '已确认') returning id",
            bridge_id_, confirmed_year_id_);
    }

    void TearDown() override {
        if (client_ == nullptr) return;
        client_->execSqlSync("delete from import_records where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from archived_files where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from defect_observations where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from condition_ratings where inspection_year_id in ($1::uuid, $2::uuid, $3::uuid)",
                             confirmed_year_id_, superseded_year_id_, pending_year_id_);
        if (!assessment_run_id_.empty()) {
            client_->execSqlSync(
                "alter table assessment_runs disable trigger trg_assessment_runs_completed_formal_immutable");
            client_->execSqlSync("delete from assessment_runs where id=$1::uuid", assessment_run_id_);
            client_->execSqlSync(
                "alter table assessment_runs enable trigger trg_assessment_runs_completed_formal_immutable");
        }
        client_->execSqlSync("delete from defect_threads where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from bridge_components where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync(
            "delete from bridge_component_inventory_revisions where id=$1::uuid",
            inventory_revision_id_);
        // 先删父桥梁，让数据库级联清除任何测试过程中新增、但未被上面逐项跟踪的年度事实。
        // 否则正式年度仍引用规范组合时，规范组合保护触发器会拒绝删除并遗留测试包。
        client_->execSqlSync("delete from bridges where id = $1::uuid", bridge_id_);
        if (!standard_user_id_.empty()) {
            client_->execSqlSync(
                "delete from project_standard_profiles where created_by_user_id=$1::uuid",
                standard_user_id_);
        }
        if (!standard_user_id_.empty()) {
            client_->execSqlSync("delete from users where id=$1::uuid", standard_user_id_);
        }
        client_->closeAll();
        std::filesystem::remove_all(upload_root_);
    }

    template <typename... Args>
    std::string insert_id(const std::string& sql, Args&&... args) {
        const auto rows = client_->execSqlSync(sql, std::forward<Args>(args)...);
        return rows[0]["id"].template as<std::string>();
    }

    std::string insert_observation(const std::optional<std::string>& thread_id, const std::string& type) {
        return insert_id(
            "insert into defect_observations (inspection_year_id, bridge_id, bridge_component_id, defect_thread_id, "
            "structure_part, defect_type, defect_location, defect_description_raw, review_status) "
            "values ($1::uuid, $2::uuid, $3::uuid, $4::uuid, '上部结构', $5, '左侧端部', $5, '已确认') returning id",
            confirmed_year_id_, bridge_id_, component_id_, thread_id, type);
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string confirmed_year_id_;
    std::string superseded_year_id_;
    std::string pending_year_id_;
    std::string component_id_;
    std::string thread_id_;
    std::string bound_observation_id_;
    std::string unbound_observation_id_;
    std::string pending_import_id_;
    std::string confirmed_import_id_;
    std::filesystem::path upload_root_;
    std::string standard_user_id_;
    std::string technical_package_id_;
    std::string maintenance_package_id_;
    std::string rating_tree_version_id_;
    std::string standard_profile_id_;
    std::string inventory_revision_id_;
    std::string assessment_run_id_;
};

}  // namespace

TEST_F(WorkspaceRepositoryTest, BridgeOverviewUsesLatestFormalCurrentFactsAndArchiveCounts) {
    bridge_report::db::WorkspaceRepository repository(client_);

    const auto overview = repository.get_bridge_overview(bridge_id_);

    ASSERT_TRUE(overview.has_value());
    ASSERT_TRUE(overview->latest_inspection.has_value());
    EXPECT_EQ(overview->latest_inspection->inspection_year, 2025);
    EXPECT_DOUBLE_EQ(*overview->latest_inspection->overall_score, 85.61);
    ASSERT_EQ(overview->recent_inspections.size(), 1u);
    ASSERT_EQ(overview->structure_ratings.size(), 1u);
    EXPECT_EQ(overview->structure_ratings[0].rating_item_name, "上部结构");
    EXPECT_EQ(overview->pending.import_count, 1);
    EXPECT_EQ(overview->pending.unbound_observation_count, 1);
    EXPECT_EQ(overview->defect_archive.component_count, 1);
    EXPECT_EQ(overview->defect_archive.thread_count, 1);
    EXPECT_EQ(overview->defect_archive.unbound_observation_count, 1);
}

TEST_F(WorkspaceRepositoryTest, InspectionWorkspaceIsolatesImportsAndReusesReviewStatistics) {
    client_->execSqlSync(
        "update import_records set error_message='Python 返回的具体解析错误' where id=$1::uuid",
        pending_import_id_);
    bridge_report::db::WorkspaceRepository repository(client_);

    const auto workspace = repository.get_inspection_workspace(pending_year_id_);

    ASSERT_TRUE(workspace.has_value());
    EXPECT_EQ(workspace->bridge.id, bridge_id_);
    EXPECT_EQ(workspace->inspection_year.inspection_year, 2027);
    ASSERT_EQ(workspace->imports.size(), 1u);
    EXPECT_EQ(workspace->imports[0].id, pending_import_id_);
    EXPECT_EQ(workspace->imports[0].statistics.defect_count, 1);
    EXPECT_EQ(workspace->imports[0].statistics.rating_item_count, 0);
    EXPECT_EQ(workspace->imports[0].statistics.pending_count, 1);
    ASSERT_TRUE(workspace->imports[0].error_message.has_value());
    EXPECT_EQ(*workspace->imports[0].error_message, "Python 返回的具体解析错误");
    EXPECT_EQ(workspace->pending.import_count, 1);
    EXPECT_EQ(workspace->pending.unbound_observation_count, 0);
}

TEST_F(WorkspaceRepositoryTest, BridgeListCarriesLatestFormalSummaryAndPendingCount) {
    bridge_report::db::ReviewRepository repository(client_);
    const auto bridges = repository.list_bridges();

    const auto found = std::find_if(bridges.begin(), bridges.end(), [this](const auto& item) {
        return item.id == bridge_id_;
    });
    ASSERT_NE(found, bridges.end());
    ASSERT_TRUE(found->latest_inspection_year.has_value());
    EXPECT_EQ(*found->latest_inspection_year, 2025);
    EXPECT_DOUBLE_EQ(*found->latest_overall_score, 85.61);
    EXPECT_EQ(*found->latest_overall_grade, "2类");
    EXPECT_EQ(found->pending_count, 2);
}

TEST_F(WorkspaceRepositoryTest, UnknownIdsReturnNoWorkspace) {
    bridge_report::db::WorkspaceRepository repository(client_);
    EXPECT_FALSE(repository.get_bridge_overview("11111111-1111-1111-1111-111111111111").has_value());
    EXPECT_FALSE(repository.get_inspection_workspace("22222222-2222-2222-2222-222222222222").has_value());
}

TEST_F(WorkspaceRepositoryTest, CreatesAnnualInspectionAndReturnsExistingOnDuplicate) {
    bridge_report::db::WorkspaceRepository repository(client_);

    const auto created = repository.create_inspection_year(
        bridge_id_, 2030, rating_tree_version_id_, standard_user_id_);
    ASSERT_EQ(created.status, bridge_report::db::CreateInspectionYearStatus::Created);
    ASSERT_TRUE(created.inspection_year.has_value());
    EXPECT_EQ(created.inspection_year->inspection_year, 2030);
    EXPECT_EQ(created.inspection_year->status, "待校对");
    EXPECT_TRUE(created.inspection_year->is_current);
    const auto workspace = repository.get_inspection_workspace(created.inspection_year->id);
    ASSERT_TRUE(workspace.has_value());
    ASSERT_TRUE(workspace->standard_profile.has_value());
    EXPECT_EQ(workspace->standard_profile->technical_condition.standard_code, "TEST H21");
    EXPECT_EQ(workspace->standard_profile->maintenance.standard_code, "TEST 5120");
    EXPECT_EQ(
        workspace->standard_profile->rating_tree_version_id,
        rating_tree_version_id_);

    const auto duplicate = repository.create_inspection_year(
        bridge_id_, 2030, rating_tree_version_id_, standard_user_id_);
    ASSERT_EQ(duplicate.status, bridge_report::db::CreateInspectionYearStatus::AlreadyExists);
    ASSERT_TRUE(duplicate.existing_inspection_year_id.has_value());
    EXPECT_EQ(*duplicate.existing_inspection_year_id, created.inspection_year->id);
}

TEST_F(WorkspaceRepositoryTest, CreateAnnualInspectionRejectsUnknownBridge) {
    bridge_report::db::WorkspaceRepository repository(client_);
    const auto outcome = repository.create_inspection_year(
        "11111111-1111-1111-1111-111111111111", 2030,
        rating_tree_version_id_, standard_user_id_);
    EXPECT_EQ(outcome.status, bridge_report::db::CreateInspectionYearStatus::BridgeNotFound);
    EXPECT_FALSE(outcome.inspection_year.has_value());
}

TEST_F(WorkspaceRepositoryTest, CreateAnnualInspectionRequiresAvailablePublishedTree) {
    bridge_report::db::WorkspaceRepository repository(client_);
    const auto missing = repository.create_inspection_year(
        bridge_id_, 2031, "11111111-1111-1111-1111-111111111111", standard_user_id_);
    EXPECT_EQ(
        missing.status,
        bridge_report::db::CreateInspectionYearStatus::RatingTreeNotFound);

    client_->execSqlSync(
        "update standard_packages set is_enabled=false where id=$1::uuid",
        maintenance_package_id_);
    const auto unavailable = repository.create_inspection_year(
        bridge_id_, 2032, rating_tree_version_id_, standard_user_id_);
    EXPECT_EQ(
        unavailable.status,
        bridge_report::db::CreateInspectionYearStatus::RatingTreeUnavailable);
}

TEST_F(WorkspaceRepositoryTest, UploadWordCreatesFlatTemporarySourceWithoutArchivedFile) {
    const std::string content = "fake-docx-content";
    const auto validation = bridge_report::archive::validate_word_input(
        R"(C:\fakepath\Q202406002-JZ-506大桥定期检测报告—（2类）.DOCX)", content, 1024);
    ASSERT_TRUE(validation.ok());
    bridge_report::db::WorkspaceRepository repository(client_);

    const auto outcome = repository.upload_word_import(
        pending_year_id_, "正式Word", validation.metadata, content, upload_root_);

    ASSERT_EQ(outcome.status, bridge_report::db::UploadWordStatus::Created);
    ASSERT_TRUE(outcome.import_record.has_value());
    EXPECT_EQ(outcome.import_record->import_status, "已上传");
    const auto response_json = outcome.import_record->to_json();
    EXPECT_FALSE(response_json.isMember("storage_relative_path"));
    EXPECT_FALSE(response_json.isMember("absolute_path"));
    const auto rows = client_->execSqlSync(
        "select ir.main_file_id::text as main_file_id, sf.id::text as source_file_id, "
        "sf.system_number, sf.original_file_name, sf.storage_relative_path, sf.file_hash, "
        "sf.file_size_bytes, sf.status, "
        "(select count(*) from archived_files af where af.bridge_id = ir.bridge_id and af.file_type = 'Word文档') "
        "as archived_word_count "
        "from import_records ir join import_source_files sf on sf.import_record_id = ir.id "
        "where ir.id = $1::uuid", outcome.import_record->id);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0]["main_file_id"].isNull());
    EXPECT_EQ(rows[0]["archived_word_count"].as<long long>(), 0);
    EXPECT_EQ(rows[0]["status"].as<std::string>(), "待解析");
    EXPECT_EQ(rows[0]["original_file_name"].as<std::string>(),
              "Q202406002-JZ-506大桥定期检测报告—（2类）.DOCX");
    EXPECT_EQ(rows[0]["file_hash"].as<std::string>(), validation.metadata.sha256);
    EXPECT_EQ(rows[0]["file_size_bytes"].as<long long>(), static_cast<long long>(content.size()));
    const auto relative = std::filesystem::path(rows[0]["storage_relative_path"].as<std::string>());
    EXPECT_FALSE(relative.is_absolute());
    EXPECT_EQ(relative.parent_path(), std::filesystem::path());
    EXPECT_EQ(relative.extension(), ".docx");
    const auto archived = bridge_report::archive::resolve_path_under_root(upload_root_, relative);
    EXPECT_TRUE(std::filesystem::is_regular_file(archived));
}

TEST_F(WorkspaceRepositoryTest, UploadWordRejectsMissingOrNonCurrentInspectionYear) {
    const std::string content = "fake-docx-content";
    const auto validation = bridge_report::archive::validate_word_input("年度报告.docx", content, 1024);
    ASSERT_TRUE(validation.ok());
    bridge_report::db::WorkspaceRepository repository(client_);

    EXPECT_EQ(repository.upload_word_import(
                  "11111111-1111-1111-1111-111111111111", "正式Word", validation.metadata,
                  content, upload_root_).status,
              bridge_report::db::UploadWordStatus::InspectionYearNotFound);
    EXPECT_EQ(repository.upload_word_import(
                  superseded_year_id_, "正式Word", validation.metadata, content, upload_root_).status,
              bridge_report::db::UploadWordStatus::InspectionYearNotCurrent);
}
