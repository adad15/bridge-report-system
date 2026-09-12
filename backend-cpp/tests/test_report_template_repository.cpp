#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ReportTemplateRepository.hpp"

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

using bridge_report::report::TemplateWriteStatus;

// 报告模板仓储的数据库集成夹具（设计 §7.1、§17.4）。
//
// 这里测的都是"不能交给调用方自觉遵守"的规则：校验没过不许启用、同时只能有一个
// 默认模板、被年度配置引用的不许删、替换文件后旧文件只有确认无人引用才准清理。
class ReportTemplateRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        client_ = shared_test_client();

        suffix_ = std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "_" +
                  ::testing::UnitTest::GetInstance()->current_test_info()->name();
        user_id_ = insert_returning_id(
            "insert into users (username, display_name, password_hash, role) "
            "values ($1, '模板测试管理员', 'not-a-real-hash', 'admin') returning id",
            "template_admin_" + suffix_);
        bridge_id_ = insert_returning_id(
            "insert into bridges (bridge_name) values ('模板测试桥') returning id");
        inspection_year_id_ = insert_returning_id(
            "insert into inspection_years (bridge_id, inspection_year, status) "
            "values ($1::uuid, 2026, '已确认') returning id",
            bridge_id_);
    }

    void TearDown() override {
        if (client_ == nullptr) return;
        client_->execSqlSync(
            "delete from inspection_report_settings where inspection_year_id=$1::uuid",
            inspection_year_id_);
        // 先摘引用再删文件：外键是 RESTRICT。
        client_->execSqlSync(
            "delete from report_templates where template_code like $1", "T_" + suffix_ + "%");
        client_->execSqlSync(
            "delete from archived_files where storage_relative_path like $1",
            "templates/" + suffix_ + "/%");
        client_->execSqlSync("delete from inspection_years where id=$1::uuid", inspection_year_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from users where id=$1::uuid", user_id_);
    }

    template <typename... Args>
    std::string insert_returning_id(const std::string& sql, Args&&... args) {
        const auto result = client_->execSqlSync(sql, std::forward<Args>(args)...);
        return result[0]["id"].template as<std::string>();
    }

    bridge_report::db::ReportTemplateRepository repository() {
        return bridge_report::db::ReportTemplateRepository(client_);
    }

    bridge_report::report::ReportTemplateInput input(const std::string& code) {
        bridge_report::report::ReportTemplateInput value;
        value.template_code = "T_" + suffix_ + "_" + code;
        value.template_name = "测试模板 " + code;
        value.contract_type = "periodic_inspection_v1";
        value.contract_config = Json::Value(Json::objectValue);
        value.contract_config["table_number_formats"] = Json::Value(Json::objectValue);
        value.updated_by_user_id = user_id_;
        return value;
    }

    /// 校验和必须是真的十六进制：file_checksum 有 ^sha256:[0-9a-f]{64}$ 约束，
    /// 拿 tag 的首字母重复 64 次会在 'g'、'o'、's' 这些非 hex 字母上直接被拒。
    static std::string hex_checksum(const std::string& tag) {
        static constexpr char kHex[] = "0123456789abcdef";
        const std::string seed = tag.empty() ? std::string("0") : tag;
        std::string digest;
        digest.reserve(64);
        for (std::size_t i = 0; i < 64; ++i) {
            digest.push_back(kHex[(static_cast<unsigned char>(seed[i % seed.size()]) + i) % 16]);
        }
        return "sha256:" + digest;
    }

    bridge_report::report::TemplateFileInput file(const std::string& tag) {
        bridge_report::report::TemplateFileInput value;
        value.original_file_name = tag + ".docx";
        value.storage_relative_path = "templates/" + suffix_ + "/" + tag + ".docx";
        value.checksum = hex_checksum(tag);
        value.size_bytes = 4096;
        return value;
    }

    /// 读回模板；读不到就报失败并返回默认值，避免对空 optional 解引用把测试进程打崩。
    bridge_report::report::ReportTemplate reload(const std::string& id) {
        const auto found = repository().find(id);
        if (!found.has_value()) {
            ADD_FAILURE() << "模板读不回来：" << id;
            return {};
        }
        return *found;
    }

    bridge_report::report::TemplateValidationOutcome validation(bool ok) {
        bridge_report::report::TemplateValidationOutcome outcome;
        outcome.is_valid = ok;
        outcome.result = Json::Value(Json::objectValue);
        outcome.result["status"] = ok ? "valid" : "invalid";
        outcome.result["issues"] = Json::Value(Json::arrayValue);
        return outcome;
    }

    /// 建一套已经通过校验并启用的模板，供后续规则测试使用。
    bridge_report::report::ReportTemplate enabled_template(const std::string& code) {
        auto repo = repository();
        auto status = TemplateWriteStatus::NotFound;
        const auto created = repo.create(input(code), file(code), validation(true), status);
        EXPECT_EQ(status, TemplateWriteStatus::Ok);
        if (!created.has_value()) {
            ADD_FAILURE() << "模板创建失败：" << code;
            return {};
        }
        EXPECT_EQ(repo.set_enabled(created->id, true, user_id_), TemplateWriteStatus::Ok);
        return reload(created->id);
    }

    void use_template_in_a_year(const std::string& template_id) {
        client_->execSqlSync(
            "insert into inspection_report_settings (inspection_year_id, template_id) "
            "values ($1::uuid, $2::uuid)",
            inspection_year_id_, template_id);
    }

    drogon::orm::DbClientPtr client_;
    std::string suffix_;
    std::string user_id_;
    std::string bridge_id_;
    std::string inspection_year_id_;
};

TEST_F(ReportTemplateRepositoryTest, NewTemplatesStartDisabledEvenWhenValid) {
    auto repo = repository();
    auto status = TemplateWriteStatus::NotFound;
    const auto created = repo.create(input("a"), file("a"), validation(true), status);

    ASSERT_EQ(status, TemplateWriteStatus::Ok);
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created->validation_status, "valid");
    // 上线是管理员看过校验结论之后的动作，不能因为校验通过就自动生效。
    EXPECT_FALSE(created->is_enabled);
    EXPECT_FALSE(created->is_default);
    EXPECT_EQ(created->usage_count, 0);
}

TEST_F(ReportTemplateRepositoryTest, DuplicateTemplateCodeIsRejected) {
    auto repo = repository();
    auto status = TemplateWriteStatus::NotFound;
    ASSERT_TRUE(repo.create(input("dup"), file("x"), validation(true), status).has_value());

    const auto again = repo.create(input("dup"), file("y"), validation(true), status);

    EXPECT_EQ(status, TemplateWriteStatus::DuplicateCode);
    EXPECT_FALSE(again.has_value());
}

TEST_F(ReportTemplateRepositoryTest, InvalidTemplateCannotBeEnabled) {
    auto repo = repository();
    auto status = TemplateWriteStatus::NotFound;
    const auto created = repo.create(input("bad"), file("b"), validation(false), status);
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created->validation_status, "invalid");

    EXPECT_EQ(repo.set_enabled(created->id, true, user_id_), TemplateWriteStatus::NotValidated);
    EXPECT_FALSE(reload(created->id).is_enabled);
}

TEST_F(ReportTemplateRepositoryTest, InvalidTemplateCannotBecomeDefault) {
    auto repo = repository();
    auto status = TemplateWriteStatus::NotFound;
    const auto created = repo.create(input("bad2"), file("c"), validation(false), status);
    ASSERT_TRUE(created.has_value());

    EXPECT_EQ(repo.set_default(created->id, user_id_), TemplateWriteStatus::NotValidated);
}

TEST_F(ReportTemplateRepositoryTest, SettingDefaultClearsThePreviousDefault) {
    auto repo = repository();
    const auto first = enabled_template("d1");
    const auto second = enabled_template("d2");

    ASSERT_EQ(repo.set_default(first.id, user_id_), TemplateWriteStatus::Ok);
    ASSERT_EQ(repo.set_default(second.id, user_id_), TemplateWriteStatus::Ok);

    // 数据库上有部分唯一索引兜底，但换默认必须在一个事务里完成，
    // 分两步做中间那一刻会撞索引。
    EXPECT_FALSE(reload(first.id).is_default);
    EXPECT_TRUE(reload(second.id).is_default);
    const auto current = repo.find_default();
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->id, second.id);
}

TEST_F(ReportTemplateRepositoryTest, SettingDefaultAlsoEnablesTheTemplate) {
    auto repo = repository();
    auto status = TemplateWriteStatus::NotFound;
    const auto created = repo.create(input("d3"), file("e"), validation(true), status);
    ASSERT_TRUE(created.has_value());
    ASSERT_FALSE(created->is_enabled);

    ASSERT_EQ(repo.set_default(created->id, user_id_), TemplateWriteStatus::Ok);

    // 默认必然是启用的，否则普通用户选不到它。
    const auto reloaded = repo.find(created->id);
    EXPECT_TRUE(reloaded->is_enabled);
    EXPECT_TRUE(reloaded->is_default);
}

TEST_F(ReportTemplateRepositoryTest, TheDefaultTemplateCannotBeDisabledDirectly) {
    auto repo = repository();
    const auto item = enabled_template("d4");
    ASSERT_EQ(repo.set_default(item.id, user_id_), TemplateWriteStatus::Ok);

    EXPECT_EQ(repo.set_enabled(item.id, false, user_id_), TemplateWriteStatus::IsDefaultTemplate);
    EXPECT_TRUE(reload(item.id).is_enabled);
}

TEST_F(ReportTemplateRepositoryTest, TheDefaultTemplateCannotBeDeleted) {
    auto repo = repository();
    const auto item = enabled_template("d5");
    ASSERT_EQ(repo.set_default(item.id, user_id_), TemplateWriteStatus::Ok);

    std::optional<std::string> obsolete;
    EXPECT_EQ(repo.remove(item.id, obsolete), TemplateWriteStatus::IsDefaultTemplate);
    EXPECT_FALSE(obsolete.has_value());
    EXPECT_TRUE(repo.find(item.id).has_value());
}

TEST_F(ReportTemplateRepositoryTest, TemplateUsedByAYearConfigurationCannotBeDeleted) {
    auto repo = repository();
    const auto item = enabled_template("used");
    use_template_in_a_year(item.id);

    std::optional<std::string> obsolete;
    EXPECT_EQ(repo.remove(item.id, obsolete), TemplateWriteStatus::Referenced);
    EXPECT_EQ(reload(item.id).usage_count, 1);
}

TEST_F(ReportTemplateRepositoryTest, DeletingAnUnusedTemplateReleasesItsFile) {
    auto repo = repository();
    auto status = TemplateWriteStatus::NotFound;
    const auto created = repo.create(input("gone"), file("g"), validation(true), status);
    ASSERT_TRUE(created.has_value());

    std::optional<std::string> obsolete;
    ASSERT_EQ(repo.remove(created->id, obsolete), TemplateWriteStatus::Ok);

    // 文件无人引用，路径交还调用方去删磁盘；归档行本身已经删掉。
    ASSERT_TRUE(obsolete.has_value());
    EXPECT_EQ(*obsolete, "templates/" + suffix_ + "/g.docx");
    const auto remaining = client_->execSqlSync(
        "select count(*) as n from archived_files where id=$1::uuid", created->file_id);
    EXPECT_EQ(remaining[0]["n"].as<int>(), 0);
}

TEST_F(ReportTemplateRepositoryTest, ReplacingTheFileSwapsAtomicallyAndReleasesTheOldOne) {
    auto repo = repository();
    const auto item = enabled_template("swap");
    const auto old_file_id = item.file_id;

    const auto replacement = repo.replace_file(
        item.id, file("swapped"), validation(true), user_id_);

    ASSERT_EQ(replacement.status, TemplateWriteStatus::Ok);
    ASSERT_TRUE(replacement.obsolete_storage_relative_path.has_value());
    EXPECT_EQ(*replacement.obsolete_storage_relative_path,
              "templates/" + suffix_ + "/swap.docx");

    const auto reloaded = reload(item.id);
    EXPECT_NE(reloaded.file_id, old_file_id);
    EXPECT_EQ(reloaded.file_checksum, hex_checksum("swapped"));
    // 旧归档行已经删掉，模板仍然启用（新文件校验通过）。
    const auto remaining = client_->execSqlSync(
        "select count(*) as n from archived_files where id=$1::uuid", old_file_id);
    EXPECT_EQ(remaining[0]["n"].as<int>(), 0);
    EXPECT_TRUE(reloaded.is_enabled);
}

TEST_F(ReportTemplateRepositoryTest, ReplacingWithAnInvalidFileTakesTheTemplateOffline) {
    auto repo = repository();
    const auto item = enabled_template("offline");
    ASSERT_TRUE(reload(item.id).is_enabled);

    const auto replacement = repo.replace_file(
        item.id, file("broken"), validation(false), user_id_);

    ASSERT_EQ(replacement.status, TemplateWriteStatus::Ok);
    const auto reloaded = repo.find(item.id);
    EXPECT_EQ(reloaded->validation_status, "invalid");
    // 一套已知不合规的模板不能继续被普通用户选中。
    EXPECT_FALSE(reloaded->is_enabled);
    EXPECT_FALSE(reloaded->is_default);
}

TEST_F(ReportTemplateRepositoryTest, EditingTheConfigIntoAnInvalidStateTakesTheTemplateOffline) {
    auto repo = repository();
    const auto item = enabled_template("cfg");

    auto changed = input("cfg");
    changed.template_name = "改了编号格式";
    auto status = TemplateWriteStatus::NotFound;
    const auto updated = repo.update(item.id, changed, validation(false), status);

    ASSERT_EQ(status, TemplateWriteStatus::Ok);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->validation_status, "invalid");
    EXPECT_FALSE(updated->is_enabled);
}

TEST_F(ReportTemplateRepositoryTest, OnlyEnabledTemplatesAppearInTheUserFacingListing) {
    auto repo = repository();
    const auto visible = enabled_template("on");
    auto status = TemplateWriteStatus::NotFound;
    const auto hidden = repo.create(input("off"), file("h"), validation(true), status);
    ASSERT_TRUE(hidden.has_value());

    const auto contains = [](const auto& list, const std::string& id) {
        for (const auto& item : list) {
            if (item.id == id) return true;
        }
        return false;
    };
    EXPECT_TRUE(contains(repo.list(true), visible.id));
    EXPECT_FALSE(contains(repo.list(true), hidden->id));
    EXPECT_TRUE(contains(repo.list(false), hidden->id));
}

}  // namespace
