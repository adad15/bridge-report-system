#include <cstdlib>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ReportDirectoryRepository.hpp"

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

using bridge_report::report::DirectoryDeleteStatus;

// 报告人员库与设备库的数据库集成夹具（设计 §15.1、§15.2）。
//
// 重点在生命周期规则：被年度报告配置引用之后只能停用、不能硬删除。删除接口要把
// 这件事作为业务结论返回，而不是让外键异常穿到上层——上层只会拿到一句"数据库错误"。
class ReportDirectoryRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        client_ = shared_test_client();

        bridge_id_ = insert_returning_id(
            "insert into bridges (bridge_name) values ('报告资料库测试桥') returning id");
        inspection_year_id_ = insert_returning_id(
            "insert into inspection_years (bridge_id, inspection_year, status) "
            "values ($1::uuid, 2026, '已确认') returning id",
            bridge_id_);
    }

    void TearDown() override {
        if (client_ == nullptr) return;
        client_->execSqlSync(
            "delete from inspection_report_personnel where inspection_year_id=$1::uuid",
            inspection_year_id_);
        client_->execSqlSync(
            "delete from inspection_report_equipment where inspection_year_id=$1::uuid",
            inspection_year_id_);
        for (const auto& id : personnel_ids_) {
            client_->execSqlSync("delete from report_personnel where id=$1::uuid", id);
        }
        for (const auto& id : equipment_ids_) {
            client_->execSqlSync("delete from report_equipment where id=$1::uuid", id);
        }
        client_->execSqlSync("delete from inspection_years where id=$1::uuid", inspection_year_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
    }

    template <typename... Args>
    std::string insert_returning_id(const std::string& sql, Args&&... args) {
        const auto result = client_->execSqlSync(sql, std::forward<Args>(args)...);
        return result[0]["id"].template as<std::string>();
    }

    bridge_report::db::ReportDirectoryRepository repository() {
        return bridge_report::db::ReportDirectoryRepository(client_);
    }

    bridge_report::report::ReportPersonnelInput person_input(std::string name) {
        bridge_report::report::ReportPersonnelInput input;
        input.full_name = std::move(name);
        input.organization = "某某交通科学研究院";
        input.professional_title = "高级工程师";
        return input;
    }

    bridge_report::report::ReportEquipmentInput equipment_input(std::string name) {
        bridge_report::report::ReportEquipmentInput input;
        input.equipment_name = std::move(name);
        input.model_spec = "ZBL-F103";
        return input;
    }

    void assign_person(const std::string& personnel_id, const std::string& role) {
        client_->execSqlSync(
            "insert into inspection_report_personnel (inspection_year_id, personnel_id, role_code) "
            "values ($1::uuid, $2::uuid, $3)",
            inspection_year_id_, personnel_id, role);
    }

    void assign_equipment(const std::string& equipment_id) {
        client_->execSqlSync(
            "insert into inspection_report_equipment (inspection_year_id, equipment_id) "
            "values ($1::uuid, $2::uuid)",
            inspection_year_id_, equipment_id);
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string inspection_year_id_;
    std::vector<std::string> personnel_ids_;
    std::vector<std::string> equipment_ids_;
};

TEST_F(ReportDirectoryRepositoryTest, CreatesAndReadsBackPersonnel) {
    auto repo = repository();
    const auto created = repo.create_personnel(person_input("张三"));
    personnel_ids_.push_back(created.id);

    EXPECT_EQ(created.full_name, "张三");
    ASSERT_TRUE(created.organization.has_value());
    EXPECT_EQ(*created.organization, "某某交通科学研究院");
    EXPECT_TRUE(created.is_enabled);
    EXPECT_EQ(created.assignment_count, 0);

    const auto found = repo.find_personnel(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->id, created.id);
}

TEST_F(ReportDirectoryRepositoryTest, BlankOptionalFieldsBecomeNullNotWhitespace) {
    auto repo = repository();
    auto input = person_input("李四");
    input.phone = "   ";
    const auto created = repo.create_personnel(input);
    personnel_ids_.push_back(created.id);

    // "  " 被当成有值的话，界面上会显示一个看不见的电话号码。
    EXPECT_FALSE(created.phone.has_value());
}

TEST_F(ReportDirectoryRepositoryTest, DisabledPersonnelAreHiddenFromTheEnabledOnlyListing) {
    auto repo = repository();
    const auto active = repo.create_personnel(person_input("在岗人员"));
    const auto retired = repo.create_personnel(person_input("停用人员"));
    personnel_ids_.push_back(active.id);
    personnel_ids_.push_back(retired.id);
    ASSERT_TRUE(repo.set_personnel_enabled(retired.id, false).has_value());

    const auto enabled_only = repo.list_personnel(true);
    const auto everything = repo.list_personnel(false);

    const auto contains = [](const auto& list, const std::string& id) {
        for (const auto& item : list) {
            if (item.id == id) return true;
        }
        return false;
    };
    EXPECT_TRUE(contains(enabled_only, active.id));
    EXPECT_FALSE(contains(enabled_only, retired.id));
    EXPECT_TRUE(contains(everything, retired.id));
}

TEST_F(ReportDirectoryRepositoryTest, UpdatesPersonnelAndClearsOmittedFields) {
    auto repo = repository();
    const auto created = repo.create_personnel(person_input("王五"));
    personnel_ids_.push_back(created.id);

    bridge_report::report::ReportPersonnelInput changed;
    changed.full_name = "王五（改名）";
    const auto updated = repo.update_personnel(created.id, changed);

    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->full_name, "王五（改名）");
    // 更新是整体替换：这次没给单位，就该被清掉，而不是留着上一版的值。
    EXPECT_FALSE(updated->organization.has_value());
}

TEST_F(ReportDirectoryRepositoryTest, UpdatingAnUnknownPersonnelReturnsNothing) {
    auto repo = repository();
    EXPECT_FALSE(
        repo.update_personnel("00000000-0000-0000-0000-000000000000", person_input("查无此人"))
            .has_value());
}

TEST_F(ReportDirectoryRepositoryTest, DeletesUnreferencedPersonnel) {
    auto repo = repository();
    const auto created = repo.create_personnel(person_input("未被引用"));

    EXPECT_EQ(repo.delete_personnel(created.id), DirectoryDeleteStatus::Deleted);
    EXPECT_FALSE(repo.find_personnel(created.id).has_value());
}

TEST_F(ReportDirectoryRepositoryTest, ReferencedPersonnelCannotBeHardDeleted) {
    auto repo = repository();
    const auto created = repo.create_personnel(person_input("已被年度引用"));
    personnel_ids_.push_back(created.id);
    assign_person(created.id, "approver");

    // 关键：这是业务结论，不是异常。上层要能据此提示"只能停用"（设计 §15.3）。
    EXPECT_EQ(repo.delete_personnel(created.id), DirectoryDeleteStatus::Referenced);
    ASSERT_TRUE(repo.find_personnel(created.id).has_value());
    EXPECT_EQ(repo.find_personnel(created.id)->assignment_count, 1);
}

TEST_F(ReportDirectoryRepositoryTest, DeletingAnUnknownPersonnelReportsNotFound) {
    auto repo = repository();
    EXPECT_EQ(repo.delete_personnel("00000000-0000-0000-0000-000000000000"),
              DirectoryDeleteStatus::NotFound);
}

TEST_F(ReportDirectoryRepositoryTest, CountsEveryRoleAssignmentOfTheSamePerson) {
    auto repo = repository();
    const auto created = repo.create_personnel(person_input("身兼两职"));
    personnel_ids_.push_back(created.id);
    assign_person(created.id, "approver");
    assign_person(created.id, "compiler");

    EXPECT_EQ(repo.find_personnel(created.id)->assignment_count, 2);
}

TEST_F(ReportDirectoryRepositoryTest, CreatesEquipmentWithCalibrationDate) {
    auto repo = repository();
    auto input = equipment_input("裂缝观测仪");
    input.calibration_valid_until = "2027-06-30";
    const auto created = repo.create_equipment(input);
    equipment_ids_.push_back(created.id);

    ASSERT_TRUE(created.calibration_valid_until.has_value());
    EXPECT_EQ(*created.calibration_valid_until, "2027-06-30");
    EXPECT_EQ(created.assignment_count, 0);
}

TEST_F(ReportDirectoryRepositoryTest, ReferencedEquipmentCannotBeHardDeleted) {
    auto repo = repository();
    const auto created = repo.create_equipment(equipment_input("全站仪"));
    equipment_ids_.push_back(created.id);
    assign_equipment(created.id);

    EXPECT_EQ(repo.delete_equipment(created.id), DirectoryDeleteStatus::Referenced);
    EXPECT_EQ(repo.find_equipment(created.id)->assignment_count, 1);
}

TEST_F(ReportDirectoryRepositoryTest, DeletesUnreferencedEquipment) {
    auto repo = repository();
    const auto created = repo.create_equipment(equipment_input("回弹仪"));

    EXPECT_EQ(repo.delete_equipment(created.id), DirectoryDeleteStatus::Deleted);
    EXPECT_FALSE(repo.find_equipment(created.id).has_value());
}

TEST_F(ReportDirectoryRepositoryTest, DisablingEquipmentKeepsItReadableForExistingConfigurations) {
    auto repo = repository();
    const auto created = repo.create_equipment(equipment_input("已停用设备"));
    equipment_ids_.push_back(created.id);
    assign_equipment(created.id);
    ASSERT_TRUE(repo.set_equipment_enabled(created.id, false).has_value());

    // 停用只影响"能不能被新配置选中"，历史配置仍要能显示出来（设计 §15.3）。
    const auto found = repo.find_equipment(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->is_enabled);
    EXPECT_EQ(found->assignment_count, 1);
}

}  // namespace
