#include <cstdlib>
#include <optional>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/DefectThreadRepository.hpp"

namespace {

// 线索绑定事务的数据库集成夹具。构造同一桥上：
//   * 2024/2025 两个当前有效年度 + 2024 旧修订版
//   * 构件 A（含线索与三条观测）与构件 B（用于跨构件拒绝）
class DefectThreadRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        const bridge_report::config::PostgresConfig config{};
        client_ = bridge_report::db::create_db_client(config, 1);

        bridge_id_ = insert_id("insert into bridges (bridge_name) values ('M06线索测试桥') returning id");
        year_2025_ = insert_id(
            "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current) "
            "values ($1::uuid, 2025, '已确认', 1, true) returning id", bridge_id_);
        year_2024_old_ = insert_id(
            "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current) "
            "values ($1::uuid, 2024, '已被修订', 1, false) returning id", bridge_id_);
        year_2024_ = insert_id(
            "insert into inspection_years (bridge_id, inspection_year, status, version_number, is_current) "
            "values ($1::uuid, 2024, '已确认', 2, true) returning id", bridge_id_);

        component_a_ = insert_component("上部结构", "2-1#板", "上部承重构件", "key-a");
        component_b_ = insert_component("桥面系", "伸缩缝", "伸缩缝装置", "key-b");

        thread_id_ = insert_id(
            "insert into defect_threads (bridge_id, bridge_component_id, thread_name, defect_type, defect_location, "
            "confirmation_status) values ($1::uuid, $2::uuid, '蜂窝、麻面｜左侧端部', '蜂窝、麻面', '左侧端部', "
            "'人工已确认') returning id", bridge_id_, component_a_);

        observation_2025_ = insert_observation(year_2025_, component_a_, "已确认");
        observation_2024_ = insert_observation(year_2024_, component_a_, "已修改");
        observation_revision_ = insert_observation(year_2024_old_, component_a_, "已确认");
        observation_pending_ = insert_observation(year_2025_, component_a_, "待校对");
    }

    void TearDown() override {
        if (client_ == nullptr) {
            return;
        }
        client_->execSqlSync("delete from defect_comparisons where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from defect_observations where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from defect_threads where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from bridge_components where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where id = $1::uuid", year_2024_);
        client_->execSqlSync("delete from inspection_years where id = $1::uuid", year_2024_old_);
        client_->execSqlSync("delete from inspection_years where id = $1::uuid", year_2025_);
        client_->execSqlSync("delete from bridges where id = $1::uuid", bridge_id_);
        client_->closeAll();
    }

    template <typename... Args>
    std::string insert_id(const std::string& sql, Args&&... args) {
        return client_->execSqlSync(sql, std::forward<Args>(args)...)[0]["id"].template as<std::string>();
    }

    std::string insert_component(const std::string& part, const std::string& type, const std::string& code,
                                 const std::string& key) {
        return insert_id(
            "insert into bridge_components (bridge_id, structure_part, component_type, business_component_code, "
            "normalized_component_key) values ($1::uuid, $2, $3, $4, $5) returning id",
            bridge_id_, part, type, code, key);
    }

    std::string insert_observation(const std::string& year_id, const std::string& component_id,
                                   const std::string& review_status) {
        return insert_id(
            "insert into defect_observations (inspection_year_id, bridge_id, bridge_component_id, structure_part, "
            "defect_type, defect_location, defect_description_raw, review_status) "
            "values ($1::uuid, $2::uuid, $3::uuid, '上部结构', '蜂窝、麻面', '左侧端部', '描述', $4) returning id",
            year_id, bridge_id_, component_id, review_status);
    }

    std::string token_of(const std::string& observation_id) {
        const auto rows = client_->execSqlSync(
            "select updated_at::text as updated_at from defect_observations where id = $1::uuid", observation_id);
        return rows[0]["updated_at"].as<std::string>();
    }

    std::optional<std::string> bound_thread_of(const std::string& observation_id) {
        const auto rows = client_->execSqlSync(
            "select defect_thread_id::text as thread_id from defect_observations where id = $1::uuid",
            observation_id);
        if (rows[0]["thread_id"].isNull()) {
            return std::nullopt;
        }
        return rows[0]["thread_id"].as<std::string>();
    }

    std::pair<Json::Value, Json::Value> thread_span(const std::string& thread_id) {
        const auto rows = client_->execSqlSync(
            "select fy.inspection_year as first_year, ly.inspection_year as latest_year "
            "from defect_threads t "
            "left join inspection_years fy on fy.id = t.first_seen_inspection_id "
            "left join inspection_years ly on ly.id = t.latest_seen_inspection_id "
            "where t.id = $1::uuid",
            thread_id);
        const auto& row = rows[0];
        return {
            row["first_year"].isNull() ? Json::Value(Json::nullValue) : Json::Value(row["first_year"].as<int>()),
            row["latest_year"].isNull() ? Json::Value(Json::nullValue) : Json::Value(row["latest_year"].as<int>()),
        };
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string year_2025_;
    std::string year_2024_;
    std::string year_2024_old_;
    std::string component_a_;
    std::string component_b_;
    std::string thread_id_;
    std::string observation_2025_;
    std::string observation_2024_;
    std::string observation_revision_;
    std::string observation_pending_;
};

}  // namespace

TEST_F(DefectThreadRepositoryTest, BindsTwoYearsAndRecomputesSpan) {
    bridge_report::db::DefectThreadRepository repository(client_);

    const auto first = repository.bind_observation(observation_2025_, thread_id_, token_of(observation_2025_), false);
    ASSERT_TRUE(first.success) << first.error_code << ": " << first.error_message;
    EXPECT_EQ(first.body["defect_thread_id"].asString(), thread_id_);
    EXPECT_FALSE(first.body["observation_updated_at"].asString().empty());

    const auto second = repository.bind_observation(observation_2024_, thread_id_, token_of(observation_2024_), false);
    ASSERT_TRUE(second.success) << second.error_code << ": " << second.error_message;

    const auto [first_year, latest_year] = thread_span(thread_id_);
    EXPECT_EQ(first_year.asInt(), 2024);
    EXPECT_EQ(latest_year.asInt(), 2025);
    EXPECT_EQ(second.body["defect_thread"]["first_seen_year"].asInt(), 2024);
    EXPECT_EQ(second.body["defect_thread"]["latest_seen_year"].asInt(), 2025);
}

TEST_F(DefectThreadRepositoryTest, StaleTokenIsRejectedAndSecondWriterLoses) {
    bridge_report::db::DefectThreadRepository repository(client_);
    const auto stale_token = token_of(observation_2025_);

    const auto first = repository.bind_observation(observation_2025_, thread_id_, stale_token, false);
    ASSERT_TRUE(first.success);

    // 并发模拟：第二个写入者仍持旧令牌重放，必须失败且不改变绑定。
    const auto replay = repository.bind_observation(observation_2025_, std::nullopt, stale_token, true);
    EXPECT_FALSE(replay.success);
    EXPECT_EQ(replay.error_code, "observation_revision_conflict");
    EXPECT_EQ(bound_thread_of(observation_2025_).value(), thread_id_);
}

TEST_F(DefectThreadRepositoryTest, RejectsCrossComponentRevisionAndPendingObservations) {
    bridge_report::db::DefectThreadRepository repository(client_);

    // 跨构件：构件 B 的线索不能绑定构件 A 的观测。
    const auto other_thread = insert_id(
        "insert into defect_threads (bridge_id, bridge_component_id, thread_name, defect_type, confirmation_status) "
        "values ($1::uuid, $2::uuid, '止水带｜通缝', '止水带损坏', '人工已确认') returning id",
        bridge_id_, component_b_);
    const auto cross = repository.bind_observation(observation_2025_, other_thread, token_of(observation_2025_), false);
    EXPECT_FALSE(cross.success);
    EXPECT_EQ(cross.error_code, "thread_component_mismatch");

    // 旧修订版观测不能绑定。
    const auto revision =
        repository.bind_observation(observation_revision_, thread_id_, token_of(observation_revision_), false);
    EXPECT_FALSE(revision.success);
    EXPECT_EQ(revision.error_code, "observation_not_current");

    // 待校对观测不是正式事实。
    const auto pending =
        repository.bind_observation(observation_pending_, thread_id_, token_of(observation_pending_), false);
    EXPECT_FALSE(pending.success);
    EXPECT_EQ(pending.error_code, "observation_not_formal");
}

TEST_F(DefectThreadRepositoryTest, RebindNeedsExplicitConfirmationAndUnbindRecomputesSpan) {
    bridge_report::db::DefectThreadRepository repository(client_);
    ASSERT_TRUE(repository.bind_observation(observation_2025_, thread_id_, token_of(observation_2025_), false).success);
    ASSERT_TRUE(repository.bind_observation(observation_2024_, thread_id_, token_of(observation_2024_), false).success);

    // 未显式确认的解绑被拒绝。
    const auto unconfirmed = repository.bind_observation(observation_2025_, std::nullopt, token_of(observation_2025_), false);
    EXPECT_FALSE(unconfirmed.success);
    EXPECT_EQ(unconfirmed.error_code, "rebind_confirmation_required");

    // 显式确认后解绑成功，线索首见/末见收缩到剩余绑定（仅 2024）。
    const auto unbound = repository.bind_observation(observation_2025_, std::nullopt, token_of(observation_2025_), true);
    ASSERT_TRUE(unbound.success) << unbound.error_code;
    EXPECT_FALSE(bound_thread_of(observation_2025_).has_value());
    const auto [first_year, latest_year] = thread_span(thread_id_);
    EXPECT_EQ(first_year.asInt(), 2024);
    EXPECT_EQ(latest_year.asInt(), 2024);
}

TEST_F(DefectThreadRepositoryTest, ConfirmedComparisonReferenceBlocksRebind) {
    bridge_report::db::DefectThreadRepository repository(client_);
    ASSERT_TRUE(repository.bind_observation(observation_2025_, thread_id_, token_of(observation_2025_), false).success);

    client_->execSqlSync(
        "insert into defect_comparisons (bridge_id, current_inspection_year_id, compared_inspection_year_id, "
        "current_defect_observation_id, comparison_result, confirmation_status) "
        "values ($1::uuid, $2::uuid, $3::uuid, $4::uuid, '延续', '人工已确认')",
        bridge_id_, year_2025_, year_2024_, observation_2025_);

    const auto rebind = repository.bind_observation(observation_2025_, std::nullopt, token_of(observation_2025_), true);
    EXPECT_FALSE(rebind.success);
    EXPECT_EQ(rebind.error_code, "observation_referenced_by_confirmed_comparison");
    EXPECT_EQ(bound_thread_of(observation_2025_).value(), thread_id_);
}

TEST_F(DefectThreadRepositoryTest, CreateThreadBindsFirstObservationAndRejectsBoundOne) {
    bridge_report::db::DefectThreadRepository repository(client_);

    const auto created = repository.create_thread(
        component_a_, "剥落、掉角", "小桩号立面", observation_2024_, token_of(observation_2024_), std::nullopt);
    ASSERT_TRUE(created.success) << created.error_code << ": " << created.error_message;
    EXPECT_EQ(created.body["defect_thread"]["thread_name"].asString(), "剥落、掉角｜小桩号立面");
    EXPECT_EQ(created.body["defect_thread"]["confirmation_status"].asString(), "人工已确认");
    EXPECT_EQ(created.body["defect_thread"]["first_seen_year"].asInt(), 2024);
    const auto new_thread_id = created.body["defect_thread"]["id"].asString();
    EXPECT_EQ(bound_thread_of(observation_2024_).value(), new_thread_id);

    // 已绑定观测不能作为新线索的首条观测。
    const auto rejected = repository.create_thread(
        component_a_, "蜂窝、麻面", "左侧端部", observation_2024_, token_of(observation_2024_), std::nullopt);
    EXPECT_FALSE(rejected.success);
    EXPECT_EQ(rejected.error_code, "observation_already_bound");
}
