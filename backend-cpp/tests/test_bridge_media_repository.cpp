#include <cstdlib>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/BridgeMediaRepository.hpp"
#include "bridge_report/db/DbClientFactory.hpp"

namespace {

/// 整个进程共用一个连接：max_connections 是 100，每个用例各开一个会把连接池耗尽。
drogon::orm::DbClientPtr shared_test_client() {
    static drogon::orm::DbClientPtr client = [] {
        const bridge_report::config::PostgresConfig config{};
        return bridge_report::db::create_db_client(config, 1);
    }();
    return client;
}

using bridge_report::report::BridgeMediaInput;
using bridge_report::report::BridgeMediaWriteStatus;

// 桥梁图件读写的数据库集成夹具。
//
// 关心三件事：一个槽位只有一张图、换图时旧的那张要被交回给调用方去删文件、
// 未知槽位和不存在的桥给出业务结论而不是把数据库异常抛到上层。
class BridgeMediaRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        client_ = shared_test_client();
        bridge_id_ = client_->execSqlSync(
            "insert into bridges (bridge_name) values ('桥梁图件测试桥') returning id::text as id")
            [0]["id"].as<std::string>();
    }

    void TearDown() override {
        if (client_ == nullptr) return;
        client_->execSqlSync("delete from bridge_media where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from archived_files where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
    }

    bridge_report::db::BridgeMediaRepository repository() {
        return bridge_report::db::BridgeMediaRepository(client_);
    }

    static BridgeMediaInput input(const std::string& slot, const std::string& name) {
        BridgeMediaInput value;
        value.slot = slot;
        value.original_file_name = name;
        value.storage_relative_path = "bridges/media/" + name;
        value.file_extension = ".png";
        value.file_size_bytes = 2048;
        value.file_hash = std::string(64, 'a');
        value.source = "人工上传";
        return value;
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
};

TEST_F(BridgeMediaRepositoryTest, SavesAFileAndListsIt) {
    const auto outcome = repository().save(bridge_id_, input("LOCATION_MAP", "location.png"));
    ASSERT_EQ(outcome.status, BridgeMediaWriteStatus::Ok);
    ASSERT_TRUE(outcome.saved.has_value());
    EXPECT_EQ(outcome.saved->slot, "LOCATION_MAP");
    EXPECT_EQ(outcome.saved->original_file_name, "location.png");
    EXPECT_EQ(outcome.saved->file_size_bytes, 2048u);
    EXPECT_FALSE(outcome.replaced.archived_file_id.has_value());

    const auto listed = repository().list(bridge_id_);
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed[0].id, outcome.saved->id);
    // 归档记录跟着一起建，文件用途写明它是哪个槽位的图。
    const auto files = client_->execSqlSync(
        "select file_type, file_purpose from archived_files where id=$1::uuid",
        outcome.saved->archived_file_id);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0]["file_type"].as<std::string>(), "图片");
    EXPECT_EQ(files[0]["file_purpose"].as<std::string>(), "桥梁图件·地理位置图");
}

// 一个槽位一张图：报告里每张图有固定的图号，多放一张就不知道该印哪张。
TEST_F(BridgeMediaRepositoryTest, ReplacingASlotHandsBackTheOldFile) {
    const auto first = repository().save(bridge_id_, input("LOCATION_MAP", "old.png"));
    ASSERT_EQ(first.status, BridgeMediaWriteStatus::Ok);

    auto second_input = input("LOCATION_MAP", "new.png");
    second_input.file_hash = std::string(64, 'b');
    const auto second = repository().save(bridge_id_, second_input);
    ASSERT_EQ(second.status, BridgeMediaWriteStatus::Ok);

    // 旧文件交回给调用方，它在事务提交之后才去删磁盘上那个文件。
    ASSERT_TRUE(second.replaced.archived_file_id.has_value());
    EXPECT_EQ(*second.replaced.archived_file_id, first.saved->archived_file_id);
    EXPECT_EQ(second.replaced.storage_relative_path.value_or(""), "bridges/media/old.png");

    const auto listed = repository().list(bridge_id_);
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed[0].original_file_name, "new.png");
    EXPECT_TRUE(client_->execSqlSync("select 1 from archived_files where id=$1::uuid",
                                     first.saved->archived_file_id).empty());
}

TEST_F(BridgeMediaRepositoryTest, KeepsSlotsIndependent) {
    ASSERT_EQ(repository().save(bridge_id_, input("LOCATION_MAP", "map.png")).status,
              BridgeMediaWriteStatus::Ok);
    ASSERT_EQ(repository().save(bridge_id_, input("OVERVIEW_PHOTO", "photo.png")).status,
              BridgeMediaWriteStatus::Ok);

    EXPECT_EQ(repository().list(bridge_id_).size(), 2u);
}

TEST_F(BridgeMediaRepositoryTest, RemovesBothRowsAndReportsTheFile) {
    const auto saved = repository().save(bridge_id_, input("CROSS_SECTION", "section.png"));
    ASSERT_EQ(saved.status, BridgeMediaWriteStatus::Ok);

    const auto removed = repository().remove(bridge_id_, "CROSS_SECTION");
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(removed->storage_relative_path.value_or(""), "bridges/media/section.png");
    EXPECT_TRUE(repository().list(bridge_id_).empty());
    EXPECT_TRUE(client_->execSqlSync("select 1 from archived_files where id=$1::uuid",
                                     saved.saved->archived_file_id).empty());

    // 再删一次是空操作，不是错误。
    EXPECT_FALSE(repository().remove(bridge_id_, "CROSS_SECTION").has_value());
}

TEST_F(BridgeMediaRepositoryTest, RejectsUnknownSlotsAndMissingBridges) {
    EXPECT_EQ(repository().save(bridge_id_, input("MOON_PHOTO", "moon.png")).status,
              BridgeMediaWriteStatus::UnknownSlot);

    const std::string absent = "00000000-0000-0000-0000-000000000000";
    EXPECT_EQ(repository().save(absent, input("LOCATION_MAP", "map.png")).status,
              BridgeMediaWriteStatus::BridgeNotFound);
    EXPECT_TRUE(repository().list(bridge_id_).empty());
}

TEST_F(BridgeMediaRepositoryTest, FindsOneByIdForServingContent) {
    const auto saved = repository().save(bridge_id_, input("DECK_PHOTO", "deck.png"));
    ASSERT_EQ(saved.status, BridgeMediaWriteStatus::Ok);

    const auto found = repository().find(saved.saved->id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->storage_relative_path, "bridges/media/deck.png");
    EXPECT_EQ(found->file_extension, ".png");
    EXPECT_FALSE(repository().find("00000000-0000-0000-0000-000000000000").has_value());
}

}  // namespace
