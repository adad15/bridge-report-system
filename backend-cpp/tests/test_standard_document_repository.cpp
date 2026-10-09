#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>

#include <drogon/utils/Utilities.h>
#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/StandardDocumentRepository.hpp"

namespace {

using bridge_report::db::StandardDocumentRepository;
using bridge_report::db::StandardDocumentWriteStatus;
using bridge_report::db::StandardPdfFileInput;
using bridge_report::standards::StandardDocumentInput;
using bridge_report::standards::StandardVersionInput;
using bridge_report::standards::StandardVersionKind;

/// 整个进程共用一个连接：max_connections 是 100，每个用例各开一个会把连接池耗尽。
drogon::orm::DbClientPtr shared_test_client() {
    static drogon::orm::DbClientPtr client = [] {
        const bridge_report::config::PostgresConfig config{};
        return bridge_report::db::create_db_client(config, 1);
    }();
    return client;
}

// 规范原文库仓储的数据库集成夹具。
//
// 关心三件事：唯一性冲突给出业务结论而不是数据库异常；替换和删除把要删的文件交回调用方；
// 删掉最后一个版本时规范本身留着。
class StandardDocumentRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        client_ = shared_test_client();
        run_ = drogon::utils::getUuid();
        prefix_ = "规范原文测试-" + run_ + "-";
    }

    void TearDown() override {
        if (client_ == nullptr) return;
        // 先删规范（级联删版本），归档行才删得掉。
        client_->execSqlSync("delete from standard_documents where name like $1", prefix_ + "%");
        client_->execSqlSync("delete from archived_files where storage_relative_path like $1",
                             "standard-test/" + run_ + "/%");
    }

    StandardDocumentRepository repository() const { return StandardDocumentRepository(client_); }

    StandardPdfFileInput file(const std::string& name) const {
        return {name, "standard-test/" + run_ + "/" + name, 1024, std::string(64, 'b')};
    }

    static StandardVersionInput official(const std::string& label, const std::string& effective) {
        StandardVersionInput input;
        input.label = label;
        input.code = "CODE " + label;
        input.kind = StandardVersionKind::official;
        input.effective_date = effective;
        return input;
    }

    static StandardVersionInput draft(const std::string& label) {
        StandardVersionInput input;
        input.label = label;
        input.code = "CODE " + label;
        input.kind = StandardVersionKind::draft_comment;
        return input;
    }

    StandardDocumentInput document(const std::string& suffix) const {
        return {prefix_ + suffix, std::nullopt, "测试分类"};
    }

    std::string create_document(const std::string& suffix, const StandardVersionInput& first,
                                const std::string& file_name) {
        const auto id = repository().allocate_document_id();
        const auto outcome = repository().create(id, document(suffix), first, file(file_name), "");
        EXPECT_EQ(outcome.status, StandardDocumentWriteStatus::Ok);
        return id;
    }

    drogon::orm::DbClientPtr client_;
    std::string run_;
    std::string prefix_;
};

TEST_F(StandardDocumentRepositoryTest, CreatesADocumentWithItsFirstVersion) {
    const auto id = create_document("H21", official("2011 版", "2011-09-01"), "h21.pdf");
    const auto found = repository().find(id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, prefix_ + "H21");
    ASSERT_EQ(found->versions.size(), 1u);
    EXPECT_EQ(found->versions[0].label, "2011 版");
    EXPECT_EQ(found->versions[0].effective_date, std::optional<std::string>("2011-09-01"));
    EXPECT_EQ(found->versions[0].original_file_name, "h21.pdf");
    EXPECT_FALSE(found->versions[0].nature.has_value());
}

TEST_F(StandardDocumentRepositoryTest, RejectsADuplicateDocumentName) {
    create_document("重名", official("2011 版", "2011-09-01"), "a.pdf");
    const auto id = repository().allocate_document_id();
    const auto outcome = repository().create(id, document("重名"), official("2011 版", "2011-09-01"), file("b.pdf"), "");
    EXPECT_EQ(outcome.status, StandardDocumentWriteStatus::NameTaken);
    EXPECT_FALSE(repository().find(id).has_value());
}

TEST_F(StandardDocumentRepositoryTest, RejectsDuplicateLabelsAndOfficialEffectiveDates) {
    const auto id = create_document("冲突", official("2011 版", "2011-09-01"), "a.pdf");
    EXPECT_EQ(repository().add_version(id, official("2011 版", "2021-11-01"), file("b.pdf"), "").status,
              StandardDocumentWriteStatus::LabelTaken);
    EXPECT_EQ(repository().add_version(id, official("另一版", "2011-09-01"), file("c.pdf"), "").status,
              StandardDocumentWriteStatus::EffectiveDateTaken);
    EXPECT_EQ(repository().add_version(id, draft("2017 征求意见稿"), file("d.pdf"), "").status,
              StandardDocumentWriteStatus::Ok);
}

TEST_F(StandardDocumentRepositoryTest, AddingAVersionToAMissingDocumentReportsNotFound) {
    const auto missing = repository().allocate_document_id();
    EXPECT_EQ(repository().add_version(missing, official("2011 版", "2011-09-01"), file("a.pdf"), "").status,
              StandardDocumentWriteStatus::DocumentNotFound);
}

TEST_F(StandardDocumentRepositoryTest, ListsVersionsNewestFirstUnderTheirDocument) {
    const auto id = create_document("排序", official("2004 版", "2004-10-01"), "old.pdf");
    ASSERT_EQ(repository().add_version(id, official("2021 版", "2021-11-01"), file("new.pdf"), "").status,
              StandardDocumentWriteStatus::Ok);
    const auto documents = repository().list();
    const auto found = std::find_if(documents.begin(), documents.end(),
                                    [&](const auto& item) { return item.id == id; });
    ASSERT_NE(found, documents.end());
    ASSERT_EQ(found->versions.size(), 2u);
    EXPECT_EQ(found->versions[0].label, "2021 版");
    EXPECT_EQ(found->versions[1].label, "2004 版");
}

TEST_F(StandardDocumentRepositoryTest, UpdatesVersionFieldsWithoutTouchingTheFile) {
    const auto id = create_document("编辑", official("2011 版", "2011-09-01"), "a.pdf");
    const auto before = repository().find(id)->versions[0];
    auto changed = official("2011 版（修订）", "2011-09-01");
    changed.announcement = "交通运输部公告2011年第41号";

    const auto outcome = repository().update_version(before.id, changed);
    ASSERT_EQ(outcome.status, StandardDocumentWriteStatus::Ok);
    ASSERT_TRUE(outcome.document.has_value());
    EXPECT_EQ(outcome.document->versions[0].label, "2011 版（修订）");
    EXPECT_EQ(outcome.document->versions[0].announcement, std::optional<std::string>("交通运输部公告2011年第41号"));
    EXPECT_EQ(outcome.document->versions[0].archived_file_id, before.archived_file_id);
    EXPECT_TRUE(outcome.files_to_remove.empty());
}

TEST_F(StandardDocumentRepositoryTest, ReplacingTheFileHandsBackTheOldOne) {
    const auto id = create_document("替换", official("2011 版", "2011-09-01"), "old.pdf");
    const auto version_id = repository().find(id)->versions[0].id;

    const auto outcome = repository().replace_version_file(version_id, file("new.pdf"), "");
    ASSERT_EQ(outcome.status, StandardDocumentWriteStatus::Ok);
    EXPECT_EQ(outcome.document->versions[0].original_file_name, "new.pdf");
    ASSERT_EQ(outcome.files_to_remove.size(), 1u);
    EXPECT_EQ(outcome.files_to_remove[0].storage_relative_path, "standard-test/" + run_ + "/old.pdf");
    EXPECT_TRUE(client_->execSqlSync("select 1 from archived_files where storage_relative_path = $1",
                                     "standard-test/" + run_ + "/old.pdf").empty());
}

TEST_F(StandardDocumentRepositoryTest, RemovingAVersionKeepsTheDocument) {
    const auto id = create_document("删版本", official("2011 版", "2011-09-01"), "a.pdf");
    const auto version_id = repository().find(id)->versions[0].id;

    const auto outcome = repository().remove_version(version_id);
    ASSERT_EQ(outcome.status, StandardDocumentWriteStatus::Ok);
    ASSERT_TRUE(outcome.document.has_value());
    EXPECT_TRUE(outcome.document->versions.empty());
    EXPECT_EQ(outcome.files_to_remove.size(), 1u);
    EXPECT_EQ(repository().remove_version(version_id).status, StandardDocumentWriteStatus::VersionNotFound);
}

TEST_F(StandardDocumentRepositoryTest, RemovingADocumentHandsBackEveryFile) {
    const auto id = create_document("删规范", official("2004 版", "2004-10-01"), "a.pdf");
    ASSERT_EQ(repository().add_version(id, draft("2017 征求意见稿"), file("b.pdf"), "").status,
              StandardDocumentWriteStatus::Ok);

    const auto outcome = repository().remove(id);
    ASSERT_EQ(outcome.status, StandardDocumentWriteStatus::Ok);
    EXPECT_EQ(outcome.files_to_remove.size(), 2u);
    EXPECT_FALSE(repository().find(id).has_value());
    EXPECT_EQ(repository().remove(id).status, StandardDocumentWriteStatus::DocumentNotFound);
}

TEST_F(StandardDocumentRepositoryTest, FindsTheFileOfAVersionWithItsDocumentName) {
    const auto id = create_document("内容", official("2011 版", "2011-09-01"), "a.pdf");
    const auto version_id = repository().find(id)->versions[0].id;
    const auto found = repository().find_version_file(version_id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->document_name, prefix_ + "内容");
    EXPECT_EQ(found->version.storage_relative_path, "standard-test/" + run_ + "/a.pdf");
}

}  // namespace
