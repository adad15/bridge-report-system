#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/ComponentArchiveRepository.hpp"
#include "bridge_report/db/DbClientFactory.hpp"

namespace {

// 模块 06 只读档案查询的数据库集成夹具。构造：
//   * 2025 当前有效年度（含线索绑定观测 + 构件评分校验行）
//   * 2024 当前有效年度 v2（含未绑定观测 + 1.1 风格评分行：新列全空）
//   * 2024 旧修订版 v1（is_current=false，观测只应出现在 revisions 入口）
//   * 第二个构件只在 2024 有病害：最新年度未出现也不能从列表消失
class ComponentArchiveRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        const bridge_report::config::PostgresConfig config{};
        client_ = bridge_report::db::create_db_client(config, 1);

        bridge_id_ = insert_returning_id(
            "insert into bridges (bridge_name) values ('M06档案测试桥') returning id");
        user_id_ = insert_returning_id(
            "insert into users (username, display_name, password_hash, role) "
            "values ($1, '构件档案系统评定测试员', 'not-a-real-hash', 'normal') returning id",
            "component_archive_assessment_" + bridge_id_);
        technical_package_id_ = insert_returning_id(
            "insert into standard_packages (standard_family, standard_id, standard_code, standard_name, "
            "official_edition, package_version, contract_version, algorithm_id, effective_date, content_checksum) "
            "values ('technical_condition', $1, 'TEST H21', '档案测试技术标准', '2026', '1.0.0', 1, "
            "'test-h21', '2026-01-01', $2) returning id",
            "ARCHIVE-TECH-" + bridge_id_, "sha256:" + std::string(64, 'c'));
        maintenance_package_id_ = insert_returning_id(
            "insert into standard_packages (standard_family, standard_id, standard_code, standard_name, "
            "official_edition, package_version, contract_version, algorithm_id, effective_date, content_checksum) "
            "values ('maintenance', $1, 'TEST 5120', '档案测试养护规范', '2026', '1.0.0', 1, "
            "'test-maintenance', '2026-01-01', $2) returning id",
            "ARCHIVE-MAINT-" + bridge_id_, "sha256:" + std::string(64, 'd'));
        // 迁移 019：新 profile 必须绑一份已发布评定树，且病害观测必须挂到该树的可选节点上。
        rating_tree_version_id_ = insert_returning_id(
            "insert into rating_tree_versions("
            "tree_code,tree_name,package_version,contract_version,"
            "technical_condition_package_id,technical_condition_standard_id,"
            "technical_condition_package_version,technical_condition_content_checksum,"
            "maintenance_package_id,maintenance_standard_id,"
            "maintenance_package_version,maintenance_content_checksum,"
            "organization_tree_code,organization_package_version,"
            "organization_content_checksum,tree_content_checksum,status"
            ") select $1,'档案测试评定树','1.0.0',1,$2::uuid,t.standard_id,t.package_version,t.content_checksum,"
            "       $3::uuid,m.standard_id,m.package_version,m.content_checksum,$1,'1.0.0',"
            "       'sha256:'||md5(gen_random_uuid()::text)||md5(gen_random_uuid()::text),"
            "       'sha256:'||md5(gen_random_uuid()::text)||md5(gen_random_uuid()::text),'draft' "
            "from standard_packages t, standard_packages m "
            "where t.id=$2::uuid and m.id=$3::uuid returning id",
            "ARCHIVE-TREE-" + bridge_id_, technical_package_id_, maintenance_package_id_);
        client_->execSqlSync(
            "insert into rating_tree_nodes("
            "rating_tree_version_id,node_key,display_name,node_type,scoring_mode,is_selectable"
            ") values ($1::uuid,'root','桥梁评定','root','non_scoring',false),"
            "         ($1::uuid,'defect.test','裂缝','defect','non_scoring',true)",
            rating_tree_version_id_);
        // published 要求 published_at 非空（018 的 publish_state_check），因此分两步。
        client_->execSqlSync(
            "update rating_tree_versions set status='published',published_at=now() where id=$1::uuid",
            rating_tree_version_id_);
        defect_node_id_ = client_->execSqlSync(
            "select id::text as id from rating_tree_nodes "
            "where rating_tree_version_id=$1::uuid and node_key='defect.test'",
            rating_tree_version_id_)[0]["id"].as<std::string>();
        standard_profile_id_ = insert_returning_id(
            "insert into project_standard_profiles (technical_condition_package_id, maintenance_package_id, "
            "rating_tree_version_id, created_by_user_id, change_reason) "
            "values ($1::uuid, $2::uuid, $4::uuid, $3::uuid, '档案系统评定测试') "
            "returning id",
            technical_package_id_, maintenance_package_id_, user_id_, rating_tree_version_id_);
        inventory_revision_id_ = insert_returning_id(
            "insert into bridge_component_inventory_revisions (bridge_id, revision_number, status, "
            "created_by_user_id) values ($1::uuid, 1, '草稿', $2::uuid) returning id",
            bridge_id_, user_id_);

        year_2025_ = insert_returning_id(
            "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current, "
            "standard_profile_id, component_inventory_revision_id) "
            "values ($1::uuid, 2025, '待校对', 1, true, $2::uuid, $3::uuid) returning id",
            bridge_id_, standard_profile_id_, inventory_revision_id_);
        year_2024_old_ = insert_returning_id(
            "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current) "
            "values ($1::uuid, 2024, '已被修订', 1, false) returning id", bridge_id_);
        year_2024_current_ = insert_returning_id(
            "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current, "
            "revision_source_inspection_id) "
            "values ($1::uuid, 2024, '已确认', 2, true, $2::uuid) returning id", bridge_id_, year_2024_old_);

        component_a_ = insert_returning_id(
            "insert into bridge_components (bridge_id, structure_part, component_type, business_component_code, "
            "normalized_component_key) values ($1::uuid, '上部结构', '2-1#板', '上部承重构件', "
            "'上部结构|2-1#板|上部承重构件') returning id", bridge_id_);
        component_b_ = insert_returning_id(
            "insert into bridge_components (bridge_id, structure_part, component_type, business_component_code, "
            "normalized_component_key) values ($1::uuid, '桥面系', '伸缩缝', '伸缩缝装置', "
            "'桥面系|伸缩缝|伸缩缝装置') returning id", bridge_id_);

        inventory_entry_id_ = insert_returning_id(
            "insert into bridge_component_inventory_entries (inventory_revision_id, bridge_component_id, "
            "component_number, site_name, site_component_type, sort_order) "
            "values ($1::uuid, $2::uuid, '2-1#板', '上部结构', '上部承重构件', 1) returning id",
            inventory_revision_id_, component_a_);
        client_->execSqlSync(
            "insert into bridge_component_standard_mappings (inventory_entry_id, standard_package_id, "
            "standard_bridge_type_id, standard_component_category_id, structure_part, mapping_source, "
            "confirmation_status, confirmed_by_user_id, confirmed_at) "
            "values ($1::uuid, $2::uuid, 'beam_bridge', 'superstructure.main_girder', 'superstructure', "
            "'档案测试', '已确认', $3::uuid, now())",
            inventory_entry_id_, technical_package_id_, user_id_);
        client_->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认', confirmed_by_user_id=$2::uuid, "
            "confirmed_at=now(), confirmation_note='档案系统评定测试' where id=$1::uuid",
            inventory_revision_id_, user_id_);
        client_->execSqlSync(
            "update inspection_years set status='已确认' where id=$1::uuid",
            year_2025_);

        assessment_run_id_ = insert_returning_id(
            "insert into assessment_runs (inspection_year_id, run_kind, result_status, input_summary_json, "
            "input_checksum, rule_package_summary_json, rule_package_checksum, result_summary_json, "
            "created_by_user_id, formal_revision_number, technical_condition_package_id, standard_profile_id, "
            // 迁移 019 的身份触发器：run 的评定树版本与校验和必须等于 profile 所绑的那一份。
            "component_inventory_revision_id, rating_tree_version_id, rating_tree_content_checksum) "
            "values ($1::uuid, '正式', '运行中', '{\"source\":\"archive-test\"}'::jsonb, "
            "$2, '{\"package\":\"archive-test\"}'::jsonb, $3, '{}'::jsonb, $4::uuid, "
            "1, $5::uuid, $6::uuid, $7::uuid, "
            "(select rating_tree_version_id from project_standard_profiles where id=$6::uuid), "
            "(select v.tree_content_checksum from rating_tree_versions v "
            " join project_standard_profiles p on p.rating_tree_version_id=v.id where p.id=$6::uuid)) "
            "returning id",
            year_2025_, "sha256:" + std::string(64, 'a'),
            "sha256:" + std::string(64, 'c'), user_id_, technical_package_id_,
            standard_profile_id_, inventory_revision_id_);
        client_->execSqlSync(
            "insert into assessment_component_results (assessment_run_id, bridge_component_id, "
            "standard_component_category_id, structure_part, score, deduction, result_json) "
            "values ($1::uuid, $2::uuid, 'superstructure.main_girder', 'superstructure', 55.81, 44.19, "
            "'{\"standard\":\"JTG/T H21-2011\",\"ordered_deductions\":[35.0,20.0],\"rounding_scale\":2}'::jsonb)",
            assessment_run_id_, component_a_);
        client_->execSqlSync(
            "update assessment_runs set result_status='成功', is_current=true, confirmed_by_user_id=$2::uuid, "
            "confirmed_at=now(), result_summary_json='{\"score\":55.81}'::jsonb where id=$1::uuid",
            assessment_run_id_, user_id_);

        thread_id_ = insert_returning_id(
            "insert into defect_threads (bridge_id, bridge_component_id, thread_name, defect_type, defect_location, "
            "first_seen_inspection_id, latest_seen_inspection_id, confirmation_status) "
            "values ($1::uuid, $2::uuid, '蜂窝、麻面｜左侧端部', '蜂窝、麻面', '左侧端部', "
            "$3::uuid, $3::uuid, '人工已确认') returning id", bridge_id_, component_a_, year_2025_);

        bound_observation_ = insert_observation(year_2025_, component_a_, thread_id_, "蜂窝、麻面", "2", "已确认");
        unbound_observation_ = insert_observation(year_2024_current_, component_a_, std::nullopt, "剥落、掉角", "2", "已修改");
        revision_observation_ = insert_observation(year_2024_old_, component_a_, std::nullopt, "旧版病害", "1", "已确认");
        component_b_observation_ = insert_observation(
            year_2024_current_, component_b_, std::nullopt, "止水带损坏", std::nullopt, "已确认");
        client_->execSqlSync(
            "insert into defect_measurements (defect_observation_id, measurement_type, value_type, "
            "minimum_value, maximum_value, unit, is_approximate, raw_text) "
            "values ($1::uuid, '长度', 'range', 0.5, 4.0, 'm', true, '约0.5~4.0m')",
            bound_observation_);

        // 档案评分只允许由成功的正式系统评定运行投影。
        client_->execSqlSync(
            "insert into condition_ratings (inspection_year_id, rating_level, structure_part, bridge_component_id, "
            "rating_item_name, score, review_status, assessment_run_id) "
            "values ($1::uuid, '构件', '上部结构', $2::uuid, '2-1#板', 55.81, '已确认', $3::uuid)",
            year_2025_, component_a_, assessment_run_id_);

        archived_file_id_ = insert_returning_id(
            "insert into archived_files (bridge_id, original_file_name, current_file_name, storage_relative_path, "
            "file_type, file_purpose, file_extension) "
            "values ($1::uuid, 'photo.jpg', 'photo.jpg', 'photos/m06-test.jpg', '图片', 'Word病害照片', '.jpg') "
            "returning id", bridge_id_);
        defect_photo_id_ = insert_returning_id(
            "insert into defect_photos (defect_observation_id, archived_file_id, photo_number) "
            "values ($1::uuid, $2::uuid, '2.1-1') returning id",
            bound_observation_, archived_file_id_);
    }

    void TearDown() override {
        if (client_ == nullptr) {
            return;
        }
        client_->execSqlSync("delete from defect_observations where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync(
            "delete from condition_ratings where bridge_component_id in "
            "(select id from bridge_components where bridge_id = $1::uuid)", bridge_id_);
        if (!assessment_run_id_.empty()) {
            client_->execSqlSync(
                "alter table assessment_runs disable trigger trg_assessment_runs_completed_formal_immutable");
            client_->execSqlSync("delete from assessment_runs where id = $1::uuid", assessment_run_id_);
            client_->execSqlSync(
                "alter table assessment_runs enable trigger trg_assessment_runs_completed_formal_immutable");
        }
        for (const auto* year_id : {&year_2024_current_, &year_2024_old_, &year_2025_}) {
            if (!year_id->empty()) {
                client_->execSqlSync("delete from inspection_years where id = $1::uuid", *year_id);
            }
        }
        client_->execSqlSync("delete from bridges where id = $1::uuid", bridge_id_);
        if (!standard_profile_id_.empty()) {
            client_->execSqlSync(
                "delete from project_standard_profiles where id = $1::uuid", standard_profile_id_);
        }
        if (!rating_tree_version_id_.empty()) {
            // 已发布的评定树不可变（018 的 protect_published_rating_tree_version），清场时暂停它；
            // 而评定树版本对规范包是 RESTRICT 外键，所以它必须排在规范包之前删。
            client_->execSqlSync(
                "alter table rating_tree_versions disable trigger "
                "trg_rating_tree_versions_published_immutable");
            client_->execSqlSync(
                "delete from rating_tree_versions where id = $1::uuid", rating_tree_version_id_);
            client_->execSqlSync(
                "alter table rating_tree_versions enable trigger "
                "trg_rating_tree_versions_published_immutable");
        }
        if (!technical_package_id_.empty() && !maintenance_package_id_.empty()) {
            client_->execSqlSync(
                "delete from standard_packages where id in ($1::uuid, $2::uuid)",
                technical_package_id_, maintenance_package_id_);
        }
        client_->execSqlSync("delete from users where id = $1::uuid", user_id_);
        client_->closeAll();
    }

    template <typename... Args>
    std::string insert_returning_id(const std::string& sql, Args&&... args) {
        const auto result = client_->execSqlSync(sql, std::forward<Args>(args)...);
        return result[0]["id"].template as<std::string>();
    }

    std::string insert_observation(
        const std::string& inspection_year_id,
        const std::string& component_id,
        const std::optional<std::string>& thread_id,
        const std::string& defect_type,
        const std::optional<std::string>& scale,
        const std::string& review_status
    ) {
        return insert_returning_id(
            "insert into defect_observations (inspection_year_id, bridge_id, bridge_component_id, defect_thread_id, "
            "structure_part, defect_type, defect_location, defect_description_raw, scale, review_status, "
            "rating_tree_node_id) "
            "values ($1::uuid, $2::uuid, $3::uuid, $4::uuid, '上部结构', $5, '左侧端部', $6, $7, $8, "
            "$9::uuid) returning id",
            inspection_year_id, bridge_id_, component_id, thread_id, defect_type,
            defect_type + "描述", scale, review_status, defect_node_id_);
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string year_2025_;
    std::string year_2024_current_;
    std::string year_2024_old_;
    std::string component_a_;
    std::string component_b_;
    std::string thread_id_;
    std::string bound_observation_;
    std::string unbound_observation_;
    std::string revision_observation_;
    std::string component_b_observation_;
    std::string archived_file_id_;
    std::string defect_photo_id_;
    std::string user_id_;
    std::string technical_package_id_;
    std::string maintenance_package_id_;
    std::string standard_profile_id_;
    std::string rating_tree_version_id_;
    std::string defect_node_id_;
    std::string inventory_revision_id_;
    std::string inventory_entry_id_;
    std::string assessment_run_id_;
};

const Json::Value* find_by_id(const Json::Value& items, const std::string& id) {
    for (const auto& item : items) {
        if (item["id"].asString() == id) {
            return &item;
        }
    }
    return nullptr;
}

}  // namespace

TEST_F(ComponentArchiveRepositoryTest, ListsComponentsWithCountsAndLatestScore) {
    bridge_report::db::ComponentArchiveRepository repository(client_);

    const auto body = repository.list_components(bridge_id_);

    ASSERT_EQ(body["components"].size(), 2u);
    const auto* component_a = find_by_id(body["components"], component_a_);
    ASSERT_NE(component_a, nullptr);
    EXPECT_EQ((*component_a)["thread_count"].asInt(), 1);
    EXPECT_EQ((*component_a)["unbound_count"].asInt(), 1);
    EXPECT_EQ((*component_a)["first_seen_year"].asInt(), 2024);
    EXPECT_EQ((*component_a)["latest_seen_year"].asInt(), 2025);
    EXPECT_DOUBLE_EQ((*component_a)["latest_score"].asDouble(), 55.81);
    EXPECT_EQ((*component_a)["latest_score_year"].asInt(), 2025);

    // 最新年度（2025）无病害的构件 B 仍在列表，不因未再出现而消失。
    const auto* component_b = find_by_id(body["components"], component_b_);
    ASSERT_NE(component_b, nullptr);
    EXPECT_EQ((*component_b)["latest_seen_year"].asInt(), 2024);
    EXPECT_EQ((*component_b)["thread_count"].asInt(), 0);
    EXPECT_TRUE((*component_b)["latest_score"].isNull());
}

TEST_F(ComponentArchiveRepositoryTest, DefectArchiveGroupsByThreadAndExcludesRevisions) {
    bridge_report::db::ComponentArchiveRepository repository(client_);

    const auto body = repository.get_defect_archive(component_a_);

    // 线索一级、年度二级：绑定观测进线索卡片，未绑定观测独立返回。
    ASSERT_EQ(body["threads"].size(), 1u);
    const auto& thread = body["threads"][0];
    EXPECT_EQ(thread["defect_location"].asString(), "左侧端部");
    EXPECT_EQ(thread["first_seen_year"].asInt(), 2025);
    ASSERT_EQ(thread["observations"].size(), 1u);
    EXPECT_EQ(thread["observations"][0]["id"].asString(), bound_observation_);
    EXPECT_EQ(thread["observations"][0]["scale"].asString(), "2");
    EXPECT_FALSE(thread["observations"][0].isMember("defect_deduction"));
    ASSERT_EQ(thread["observations"][0]["measurements"].size(), 1u);
    const auto& measurement = thread["observations"][0]["measurements"][0];
    EXPECT_EQ(measurement["value_type"].asString(), "range");
    EXPECT_TRUE(measurement["numeric_value"].isNull());
    EXPECT_DOUBLE_EQ(measurement["minimum_value"].asDouble(), 0.5);
    EXPECT_DOUBLE_EQ(measurement["maximum_value"].asDouble(), 4.0);
    EXPECT_TRUE(measurement["is_approximate"].asBool());
    ASSERT_EQ(thread["observations"][0]["photos"].size(), 1u);
    EXPECT_EQ(thread["observations"][0]["photos"][0]["id"].asString(), defect_photo_id_);

    ASSERT_EQ(body["unbound_observations"].size(), 1u);
    EXPECT_EQ(body["unbound_observations"][0]["id"].asString(), unbound_observation_);

    // 旧修订版观测绝不进入默认档案。
    EXPECT_EQ(find_by_id(body["unbound_observations"], revision_observation_), nullptr);

    // 档案只读取 assessment_run 投影。
    ASSERT_EQ(body["ratings"].size(), 1u);
    EXPECT_EQ(body["ratings"][0]["inspection_year"].asInt(), 2025);
    EXPECT_EQ(body["ratings"][0]["assessment_run_id"].asString(), assessment_run_id_);
    EXPECT_EQ(body["ratings"][0]["calculation_details"]["ordered_deductions"].size(), 2u);
}

TEST_F(ComponentArchiveRepositoryTest, RevisionsEntryReturnsSupersededObservationsOnly) {
    bridge_report::db::ComponentArchiveRepository repository(client_);

    const auto body = repository.get_revisions(component_a_, bridge_id_);

    ASSERT_EQ(body["revisions"].size(), 1u);
    const auto& group = body["revisions"][0];
    EXPECT_EQ(group["inspection_year"].asInt(), 2024);
    EXPECT_EQ(group["version_number"].asInt(), 1);
    EXPECT_EQ(group["inspection_status"].asString(), "已被修订");
    EXPECT_EQ(group["superseded_by_version"].asInt(), 2);
    ASSERT_EQ(group["observations"].size(), 1u);
    EXPECT_EQ(group["observations"][0]["id"].asString(), revision_observation_);
}

TEST_F(ComponentArchiveRepositoryTest, UnboundObservationsAcrossBridgeCarryComponentInfo) {
    bridge_report::db::ComponentArchiveRepository repository(client_);

    const auto body = repository.list_unbound_observations(bridge_id_);

    ASSERT_EQ(body["unbound_observations"].size(), 2u);
    const auto* unbound = find_by_id(body["unbound_observations"], unbound_observation_);
    ASSERT_NE(unbound, nullptr);
    EXPECT_EQ((*unbound)["component"]["id"].asString(), component_a_);
    EXPECT_FALSE((*unbound)["updated_at"].asString().empty());
    EXPECT_EQ(find_by_id(body["unbound_observations"], bound_observation_), nullptr);
    EXPECT_EQ(find_by_id(body["unbound_observations"], revision_observation_), nullptr);
}

TEST_F(ComponentArchiveRepositoryTest, EvidenceAndPhotoContentRefResolve) {
    bridge_report::db::ComponentArchiveRepository repository(client_);

    const auto evidence = repository.get_observation_evidence(bound_observation_);
    ASSERT_TRUE(evidence.has_value());
    EXPECT_TRUE((*evidence)["import_record_system_number"].isNull());
    EXPECT_TRUE((*evidence)["source_raw_cells"].isObject());

    const auto reference = repository.get_defect_photo_content_ref(defect_photo_id_);
    ASSERT_TRUE(reference.has_value());
    EXPECT_EQ(reference->storage_relative_path, "photos/m06-test.jpg");
    EXPECT_EQ(reference->content_type, "image/jpeg");
}
