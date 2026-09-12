#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/InspectionReportSettingsRepository.hpp"

namespace {

/// 整个进程共用一个连接：max_connections 是 100，而每个用例各开一个 DbClient 会在
/// 全量跑时把连接池耗尽（表现为满屏 "connection pointer is NULL"）。
drogon::orm::DbClientPtr shared_test_client() {
    static drogon::orm::DbClientPtr client = [] {
        const bridge_report::config::PostgresConfig config{};
        return bridge_report::db::create_db_client(config, 1);
    }();
    return client;
}

using bridge_report::report::SettingsWriteStatus;

// 年度报告配置仓储的数据库集成夹具（设计 §15.3、§12.1）。
//
// 重点在两件事：历史对比检查的候选条件必须逐条成立；停用的人员设备不能被新配置
// 选中，但已经在配置里的要照样读得出来并标记。
class InspectionReportSettingsRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        client_ = shared_test_client();

        tag_ = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        user_id_ = insert_returning_id(
            "insert into users (username, display_name, password_hash, role) "
            "values ($1, '报告配置测试员', 'not-a-real-hash', 'admin') returning id",
            "settings_" + tag_);
        bridge_id_ = insert_returning_id(
            "insert into bridges (bridge_name) values ('报告配置测试桥') returning id");
        other_bridge_id_ = insert_returning_id(
            "insert into bridges (bridge_name) values ('另一座桥') returning id");

        current_year_id_ = add_year(bridge_id_, 2026, "已确认", true);
        person_id_ = insert_returning_id(
            "insert into report_personnel (full_name) values ('签字人') returning id");
        equipment_id_ = insert_returning_id(
            "insert into report_equipment (equipment_name) values ('测试设备') returning id");
    }

    void TearDown() override {
        if (client_ == nullptr) return;
        client_->execSqlSync(
            "update inspection_years set report_comparison_inspection_id=null "
            "where bridge_id in ($1::uuid,$2::uuid)", bridge_id_, other_bridge_id_);
        for (const auto& table : {"inspection_report_personnel", "inspection_report_equipment",
                                  "inspection_report_settings"}) {
            client_->execSqlSync(
                std::string("delete from ") + table +
                " where inspection_year_id in (select id from inspection_years"
                " where bridge_id in ($1::uuid,$2::uuid))",
                bridge_id_, other_bridge_id_);
        }
        client_->execSqlSync(
            "delete from inspection_years where bridge_id in ($1::uuid,$2::uuid)",
            bridge_id_, other_bridge_id_);
        client_->execSqlSync("delete from report_personnel where id=$1::uuid", person_id_);
        client_->execSqlSync("delete from report_equipment where id=$1::uuid", equipment_id_);
        client_->execSqlSync("delete from bridges where id in ($1::uuid,$2::uuid)",
                             bridge_id_, other_bridge_id_);
        client_->execSqlSync("delete from users where id=$1::uuid", user_id_);
    }

    template <typename... Args>
    std::string insert_returning_id(const std::string& sql, Args&&... args) {
        const auto result = client_->execSqlSync(sql, std::forward<Args>(args)...);
        return result[0]["id"].template as<std::string>();
    }

    std::string add_year(const std::string& bridge, int year, const std::string& status,
                         bool is_current) {
        return insert_returning_id(
            "insert into inspection_years (bridge_id, inspection_year, status, is_current) "
            "values ($1::uuid, $2, $3, $4) returning id",
            bridge, year, status, is_current);
    }

    bridge_report::db::InspectionReportSettingsRepository repository() {
        return bridge_report::db::InspectionReportSettingsRepository(client_);
    }

    bridge_report::report::InspectionReportSettingsInput input() {
        bridge_report::report::InspectionReportSettingsInput value;
        value.configured_by_user_id = user_id_;
        return value;
    }

    std::vector<int> candidate_years() {
        std::vector<int> years;
        for (const auto& candidate : repository().list_comparison_candidates(current_year_id_)) {
            years.push_back(candidate.inspection_year);
        }
        return years;
    }

    drogon::orm::DbClientPtr client_;
    std::string tag_;
    std::string user_id_;
    std::string bridge_id_;
    std::string other_bridge_id_;
    std::string current_year_id_;
    std::string person_id_;
    std::string equipment_id_;
};

TEST_F(InspectionReportSettingsRepositoryTest, EmptyConfigurationIsReadableForANewYear) {
    const auto settings = repository().find(current_year_id_);

    ASSERT_TRUE(settings.has_value());
    EXPECT_EQ(settings->inspection_year, 2026);
    EXPECT_FALSE(settings->template_id.has_value());
    EXPECT_TRUE(settings->personnel.empty());
    EXPECT_TRUE(settings->equipment.empty());
}

TEST_F(InspectionReportSettingsRepositoryTest, UnknownYearReadsBackAsNothing) {
    EXPECT_FALSE(repository().find("00000000-0000-0000-0000-000000000000").has_value());
}

TEST_F(InspectionReportSettingsRepositoryTest, SavesAndReadsBackPersonnelAndEquipment) {
    auto value = input();
    value.personnel.push_back({person_id_, "approver", 0});
    value.personnel.push_back({person_id_, "compiler", 1});
    bridge_report::report::EquipmentAssignmentInput gear;
    gear.equipment_id = equipment_id_;
    gear.purpose = "裂缝测量";
    value.equipment.push_back(gear);

    ASSERT_EQ(repository().save(current_year_id_, value), SettingsWriteStatus::Ok);

    const auto settings = repository().find(current_year_id_);
    ASSERT_TRUE(settings.has_value());
    ASSERT_EQ(settings->personnel.size(), 2u);
    // 同一个人可以兼任两个角色。
    EXPECT_EQ(settings->personnel[0].role_code, "approver");
    EXPECT_EQ(settings->personnel[1].role_code, "compiler");
    ASSERT_EQ(settings->equipment.size(), 1u);
    ASSERT_TRUE(settings->equipment[0].purpose.has_value());
    EXPECT_EQ(*settings->equipment[0].purpose, "裂缝测量");
    EXPECT_TRUE(settings->configured_by_display_name.has_value());
}

TEST_F(InspectionReportSettingsRepositoryTest, SavingReplacesAssignmentsWholesale) {
    auto first = input();
    first.personnel.push_back({person_id_, "approver", 0});
    first.personnel.push_back({person_id_, "compiler", 1});
    ASSERT_EQ(repository().save(current_year_id_, first), SettingsWriteStatus::Ok);

    auto second = input();
    second.personnel.push_back({person_id_, "approver", 0});
    ASSERT_EQ(repository().save(current_year_id_, second), SettingsWriteStatus::Ok);

    // 全量覆盖：增量合并的话"取消 compiler 这个角色"就没法表达了。
    const auto settings = repository().find(current_year_id_);
    ASSERT_EQ(settings->personnel.size(), 1u);
    EXPECT_EQ(settings->personnel[0].role_code, "approver");
}

TEST_F(InspectionReportSettingsRepositoryTest, DisabledPersonnelCannotEnterANewConfiguration) {
    client_->execSqlSync("update report_personnel set is_enabled=false where id=$1::uuid",
                         person_id_);
    auto value = input();
    value.personnel.push_back({person_id_, "approver", 0});

    EXPECT_EQ(repository().save(current_year_id_, value), SettingsWriteStatus::PersonnelDisabled);
}

TEST_F(InspectionReportSettingsRepositoryTest,
       AlreadySavedPersonnelStaysVisibleAfterBeingDisabled) {
    auto value = input();
    value.personnel.push_back({person_id_, "approver", 0});
    ASSERT_EQ(repository().save(current_year_id_, value), SettingsWriteStatus::Ok);

    client_->execSqlSync("update report_personnel set is_enabled=false where id=$1::uuid",
                         person_id_);

    // 历史配置要看得见，否则用户不知道自己之前选了谁；但要标出来等人处理（设计 §15.3）。
    const auto settings = repository().find(current_year_id_);
    ASSERT_EQ(settings->personnel.size(), 1u);
    EXPECT_FALSE(settings->personnel[0].is_enabled);
    const auto json = settings->to_json();
    EXPECT_TRUE(json["personnel"][0]["needs_reconfirmation"].asBool());
    EXPECT_GT(json["blocking_notes"].size(), 0u);
}

TEST_F(InspectionReportSettingsRepositoryTest, DisabledEquipmentCannotEnterANewConfiguration) {
    client_->execSqlSync("update report_equipment set is_enabled=false where id=$1::uuid",
                         equipment_id_);
    auto value = input();
    bridge_report::report::EquipmentAssignmentInput gear;
    gear.equipment_id = equipment_id_;
    value.equipment.push_back(gear);

    EXPECT_EQ(repository().save(current_year_id_, value), SettingsWriteStatus::EquipmentDisabled);
}

TEST_F(InspectionReportSettingsRepositoryTest, ComparisonCandidatesFollowTheDesignConditions) {
    // 合格：同桥、当前修订、已确认、年度更早。
    add_year(bridge_id_, 2025, "已确认", true);
    // 合格：已归档也算。
    add_year(bridge_id_, 2024, "已归档", true);
    // 不合格：还没确认。
    add_year(bridge_id_, 2023, "待校对", true);
    // 不合格：已被修订，不是当前行。
    add_year(bridge_id_, 2022, "已被修订", false);
    // 不合格：晚于本次检查。
    add_year(bridge_id_, 2027, "已确认", true);
    // 不合格：别的桥。
    add_year(other_bridge_id_, 2025, "已确认", true);

    const auto years = candidate_years();

    EXPECT_EQ(years, (std::vector<int>{2025, 2024}));
}

TEST_F(InspectionReportSettingsRepositoryTest, TheYearItselfIsNeverAComparisonCandidate) {
    for (const auto& candidate : repository().list_comparison_candidates(current_year_id_)) {
        EXPECT_NE(candidate.inspection_year_id, current_year_id_);
    }
}

TEST_F(InspectionReportSettingsRepositoryTest, SavesAValidComparisonSelection) {
    const auto previous = add_year(bridge_id_, 2025, "已确认", true);
    auto value = input();
    value.comparison_inspection_id = previous;

    ASSERT_EQ(repository().save(current_year_id_, value), SettingsWriteStatus::Ok);

    const auto settings = repository().find(current_year_id_);
    ASSERT_TRUE(settings->comparison_inspection_id.has_value());
    EXPECT_EQ(*settings->comparison_inspection_id, previous);
    ASSERT_TRUE(settings->comparison_year.has_value());
    EXPECT_EQ(*settings->comparison_year, 2025);
    EXPECT_TRUE(settings->comparison_is_usable);
}

TEST_F(InspectionReportSettingsRepositoryTest, RejectsAComparisonFromAnotherBridge) {
    const auto foreign = add_year(other_bridge_id_, 2025, "已确认", true);
    auto value = input();
    value.comparison_inspection_id = foreign;

    EXPECT_EQ(repository().save(current_year_id_, value), SettingsWriteStatus::ComparisonInvalid);
}

TEST_F(InspectionReportSettingsRepositoryTest, RejectsAComparisonThatIsNotConfirmed) {
    const auto draft = add_year(bridge_id_, 2025, "待校对", true);
    auto value = input();
    value.comparison_inspection_id = draft;

    EXPECT_EQ(repository().save(current_year_id_, value), SettingsWriteStatus::ComparisonInvalid);
}

TEST_F(InspectionReportSettingsRepositoryTest, RejectsALaterYearAsComparison) {
    const auto later = add_year(bridge_id_, 2027, "已确认", true);
    auto value = input();
    value.comparison_inspection_id = later;

    EXPECT_EQ(repository().save(current_year_id_, value), SettingsWriteStatus::ComparisonInvalid);
}

TEST_F(InspectionReportSettingsRepositoryTest, AComparisonThatLaterGetsRevisedIsFlaggedUnusable) {
    const auto previous = add_year(bridge_id_, 2025, "已确认", true);
    auto value = input();
    value.comparison_inspection_id = previous;
    ASSERT_EQ(repository().save(current_year_id_, value), SettingsWriteStatus::Ok);

    // 保存之后对方被修订：选择还在，但已经不满足候选条件了。
    client_->execSqlSync(
        "update inspection_years set is_current=false, status='已被修订' where id=$1::uuid",
        previous);

    const auto settings = repository().find(current_year_id_);
    ASSERT_TRUE(settings->comparison_inspection_id.has_value());
    EXPECT_FALSE(settings->comparison_is_usable);
    EXPECT_GT(settings->to_json()["blocking_notes"].size(), 0u);
}

TEST_F(InspectionReportSettingsRepositoryTest, RejectsAnUnknownTemplate) {
    auto value = input();
    value.template_id = "00000000-0000-0000-0000-000000000000";

    EXPECT_EQ(repository().save(current_year_id_, value), SettingsWriteStatus::TemplateNotFound);
}

TEST_F(InspectionReportSettingsRepositoryTest, ClearingTheComparisonSelectionIsAllowed) {
    const auto previous = add_year(bridge_id_, 2025, "已确认", true);
    auto with_comparison = input();
    with_comparison.comparison_inspection_id = previous;
    ASSERT_EQ(repository().save(current_year_id_, with_comparison), SettingsWriteStatus::Ok);

    ASSERT_EQ(repository().save(current_year_id_, input()), SettingsWriteStatus::Ok);

    EXPECT_FALSE(repository().find(current_year_id_)->comparison_inspection_id.has_value());
}

}  // namespace
