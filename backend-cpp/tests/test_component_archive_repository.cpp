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

        year_2025_ = insert_returning_id(
            "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current) "
            "values ($1::uuid, 2025, '已确认', 1, true) returning id", bridge_id_);
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

        thread_id_ = insert_returning_id(
            "insert into defect_threads (bridge_id, bridge_component_id, thread_name, defect_type, defect_location, "
            "first_seen_inspection_id, latest_seen_inspection_id, confirmation_status) "
            "values ($1::uuid, $2::uuid, '蜂窝、麻面｜左侧端部', '蜂窝、麻面', '左侧端部', "
            "$3::uuid, $3::uuid, '人工已确认') returning id", bridge_id_, component_a_, year_2025_);

        bound_observation_ = insert_observation(year_2025_, component_a_, thread_id_, "蜂窝、麻面", "2", 35.0, "已确认");
        unbound_observation_ = insert_observation(year_2024_current_, component_a_, std::nullopt, "剥落、掉角", "2", 20.0, "已修改");
        revision_observation_ = insert_observation(year_2024_old_, component_a_, std::nullopt, "旧版病害", "1", 10.0, "已确认");
        component_b_observation_ = insert_observation(
            year_2024_current_, component_b_, std::nullopt, "止水带损坏", std::nullopt, std::nullopt, "已确认");

        // 2025 构件评分：三值校验齐全；2024 为 1.1 风格历史行（新列全空）。
        client_->execSqlSync(
            "insert into condition_ratings (inspection_year_id, rating_level, structure_part, bridge_component_id, "
            "rating_item_name, score, source_score, calculated_score, score_validation_status, "
            "calculation_details_json, review_status) "
            "values ($1::uuid, '构件', '上部结构', $2::uuid, '2-1#板', 55.81, 55.81, 55.8076118446, '一致', "
            "'{\"standard\":\"JTG/T H21-2011 4.1.1\",\"ordered_deductions\":[35.0,20.0],\"rounding_scale\":2}'::jsonb, "
            "'已确认')",
            year_2025_, component_a_);
        client_->execSqlSync(
            "insert into condition_ratings (inspection_year_id, rating_level, structure_part, bridge_component_id, "
            "rating_item_name, score, review_status) "
            "values ($1::uuid, '构件', '上部结构', $2::uuid, '2-1#板', 60.00, '已确认')",
            year_2024_current_, component_a_);

        archived_file_id_ = insert_returning_id(
            "insert into archived_files (bridge_id, original_file_name, current_file_name, storage_relative_path, "
            "file_type, file_purpose, file_extension) "
            "values ($1::uuid, 'photo.jpg', 'photo.jpg', 'photos/m06-test.jpg', '图片', 'Word病害照片', '.jpg') "
            "returning id", bridge_id_);
        defect_photo_id_ = insert_returning_id(
            "insert into defect_photos (defect_observation_id, archived_file_id, photo_number, match_status) "
            "values ($1::uuid, $2::uuid, '2.1-1', '已确认') returning id",
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
        client_->execSqlSync("delete from defect_threads where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from bridge_components where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from archived_files where id = $1::uuid", archived_file_id_);
        client_->execSqlSync("delete from inspection_years where id = $1::uuid", year_2024_current_);
        client_->execSqlSync("delete from inspection_years where id = $1::uuid", year_2024_old_);
        client_->execSqlSync("delete from inspection_years where id = $1::uuid", year_2025_);
        client_->execSqlSync("delete from bridges where id = $1::uuid", bridge_id_);
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
        const std::optional<double>& deduction,
        const std::string& review_status
    ) {
        return insert_returning_id(
            "insert into defect_observations (inspection_year_id, bridge_id, bridge_component_id, defect_thread_id, "
            "structure_part, defect_type, defect_location, defect_description_raw, scale, defect_deduction, "
            "review_status) "
            "values ($1::uuid, $2::uuid, $3::uuid, $4::uuid, '上部结构', $5, '左侧端部', $6, $7, $8, $9) "
            "returning id",
            inspection_year_id, bridge_id_, component_id, thread_id, defect_type,
            defect_type + "描述", scale, deduction, review_status);
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
    EXPECT_DOUBLE_EQ(thread["observations"][0]["defect_deduction"].asDouble(), 35.0);
    ASSERT_EQ(thread["observations"][0]["photos"].size(), 1u);
    EXPECT_EQ(thread["observations"][0]["photos"][0]["id"].asString(), defect_photo_id_);

    ASSERT_EQ(body["unbound_observations"].size(), 1u);
    EXPECT_EQ(body["unbound_observations"][0]["id"].asString(), unbound_observation_);

    // 旧修订版观测绝不进入默认档案。
    EXPECT_EQ(find_by_id(body["unbound_observations"], revision_observation_), nullptr);

    // 构件评分：2025 有完整校验明细；2024 为 1.1 历史行，明确标记缺少明细。
    ASSERT_EQ(body["ratings"].size(), 2u);
    EXPECT_EQ(body["ratings"][0]["inspection_year"].asInt(), 2025);
    EXPECT_TRUE(body["ratings"][0]["has_validation_details"].asBool());
    EXPECT_EQ(body["ratings"][0]["score_validation_status"].asString(), "一致");
    EXPECT_EQ(body["ratings"][0]["calculation_details"]["ordered_deductions"].size(), 2u);
    EXPECT_EQ(body["ratings"][1]["inspection_year"].asInt(), 2024);
    EXPECT_FALSE(body["ratings"][1]["has_validation_details"].asBool());
    EXPECT_TRUE(body["ratings"][1]["score_validation_status"].isNull());
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
