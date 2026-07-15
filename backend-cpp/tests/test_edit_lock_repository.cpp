#include <cstdlib>
#include <future>
#include <optional>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/EditLockRepository.hpp"
#include "bridge_report/db/ReviewRepository.hpp"

namespace {

class EditLockRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        const bridge_report::config::PostgresConfig config{};
        client_ = bridge_report::db::create_db_client(config, 1);

        bridge_id_ = insert_id("insert into bridges (bridge_name) values ('M06编辑锁测试桥') returning id");
        record_id_ = insert_id(
            "insert into import_records (bridge_id, import_name, source_type, import_status) "
            "values ($1::uuid, '编辑锁测试.docx', '正式Word', '待校对') returning id",
            bridge_id_);

        owner_.id = insert_id(
            "insert into users (username, display_name, password_hash, role) "
            "values ('m06_lock_owner', '张工', 'test', 'normal') returning id");
        owner_.session_id = insert_id(
            "insert into user_sessions (user_id, token_hash, expires_at) "
            "values ($1::uuid, 'm06-owner-session-1', now() + interval '1 hour') returning id",
            owner_.id);
        owner_.username = "m06_lock_owner";
        owner_.display_name = "张工";
        owner_.role = "normal";

        owner_other_tab_ = owner_;
        owner_other_tab_.session_id = insert_id(
            "insert into user_sessions (user_id, token_hash, expires_at) "
            "values ($1::uuid, 'm06-owner-session-2', now() + interval '1 hour') returning id",
            owner_.id);

        other_.id = insert_id(
            "insert into users (username, display_name, password_hash, role) "
            "values ('m06_lock_other', '李工', 'test', 'normal') returning id");
        other_.session_id = insert_id(
            "insert into user_sessions (user_id, token_hash, expires_at) "
            "values ($1::uuid, 'm06-other-session', now() + interval '1 hour') returning id",
            other_.id);
        other_.username = "m06_lock_other";
        other_.display_name = "李工";
        other_.role = "normal";

        admin_.id = insert_id(
            "insert into users (username, display_name, password_hash, role) "
            "values ('m06_lock_admin', '管理员', 'test', 'admin') returning id");
        admin_.session_id = insert_id(
            "insert into user_sessions (user_id, token_hash, expires_at) "
            "values ($1::uuid, 'm06-admin-session', now() + interval '1 hour') returning id",
            admin_.id);
        admin_.username = "m06_lock_admin";
        admin_.display_name = "管理员";
        admin_.role = "admin";
    }

    void TearDown() override {
        if (client_ == nullptr) {
            return;
        }
        client_->execSqlSync("delete from import_records where id = $1::uuid", record_id_);
        client_->execSqlSync(
            "delete from users where id in ($1::uuid, $2::uuid, $3::uuid)",
            owner_.id, other_.id, admin_.id);
        client_->execSqlSync("delete from bridges where id = $1::uuid", bridge_id_);
        client_->closeAll();
    }

    template <typename... Args>
    std::string insert_id(const std::string& sql, Args&&... args) {
        const auto result = client_->execSqlSync(sql, std::forward<Args>(args)...);
        return result[0]["id"].template as<std::string>();
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string record_id_;
    bridge_report::db::AuthUser owner_;
    bridge_report::db::AuthUser owner_other_tab_;
    bridge_report::db::AuthUser other_;
    bridge_report::db::AuthUser admin_;
};

TEST_F(EditLockRepositoryTest, OneSessionOwnsTheRecordAndNormalReleaseHandsItOver) {
    bridge_report::db::EditLockRepository repository(client_);

    const auto acquired = repository.acquire(record_id_, owner_);
    ASSERT_TRUE(acquired.acquired);
    ASSERT_FALSE(acquired.lock_token.empty());
    ASSERT_TRUE(acquired.lock.has_value());
    EXPECT_EQ(acquired.lock->display_name, "张工");
    const auto records = bridge_report::db::ReviewRepository(client_).list_import_records(bridge_id_);
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records[0].edit_lock_owner_display_name, std::optional<std::string>("张工"));
    EXPECT_TRUE(records[0].edit_lock_acquired_at.has_value());
    EXPECT_EQ(repository.check(record_id_, owner_, acquired.lock_token).status,
              bridge_report::db::EditLockCheckStatus::active);
    EXPECT_EQ(repository.heartbeat(record_id_, owner_, acquired.lock_token).status,
              bridge_report::db::EditLockCheckStatus::active);

    const auto blocked_other_user = repository.acquire(record_id_, other_);
    EXPECT_FALSE(blocked_other_user.acquired);
    ASSERT_TRUE(blocked_other_user.lock.has_value());
    EXPECT_EQ(blocked_other_user.lock->display_name, "张工");

    const auto blocked_other_tab = repository.acquire(record_id_, owner_other_tab_);
    EXPECT_FALSE(blocked_other_tab.acquired);
    EXPECT_EQ(repository.check(record_id_, owner_other_tab_, acquired.lock_token).status,
              bridge_report::db::EditLockCheckStatus::invalid);
    EXPECT_FALSE(repository.release(record_id_, owner_, "wrong-token"));
    EXPECT_TRUE(repository.release(record_id_, owner_, acquired.lock_token));

    const auto handed_over = repository.acquire(record_id_, other_);
    EXPECT_TRUE(handed_over.acquired);
}

TEST_F(EditLockRepositoryTest, ExpiredLeaseCanBeReplaced) {
    bridge_report::db::EditLockRepository repository(client_);
    const auto acquired = repository.acquire(record_id_, owner_);
    ASSERT_TRUE(acquired.acquired);

    client_->execSqlSync(
        "update import_record_edit_locks set acquired_at = now() - interval '3 minutes', "
        "last_heartbeat_at = now() - interval '3 minutes', expires_at = now() - interval '1 minute' "
        "where import_record_id = $1::uuid",
        record_id_);
    EXPECT_EQ(repository.check(record_id_, owner_, acquired.lock_token).status,
              bridge_report::db::EditLockCheckStatus::expired);

    const auto replaced = repository.acquire(record_id_, other_);
    ASSERT_TRUE(replaced.acquired);
    ASSERT_TRUE(replaced.lock.has_value());
    EXPECT_EQ(replaced.lock->user_id, other_.id);
}

TEST_F(EditLockRepositoryTest, ForceReleaseIsAuditedAndOldTokenGetsDedicatedStatus) {
    bridge_report::db::EditLockRepository repository(client_);
    const auto acquired = repository.acquire(record_id_, owner_);
    ASSERT_TRUE(acquired.acquired);

    EXPECT_TRUE(repository.force_release(record_id_, admin_, "张工异常退出，管理员交接"));
    EXPECT_EQ(repository.check(record_id_, owner_, acquired.lock_token).status,
              bridge_report::db::EditLockCheckStatus::force_released);
    EXPECT_FALSE(repository.get_active(record_id_).has_value());

    const auto events = client_->execSqlSync(
        "select action, actor_user_id::text as actor_user_id, reason "
        "from import_record_edit_lock_events where import_record_id = $1::uuid",
        record_id_);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0]["action"].as<std::string>(), "force_released");
    EXPECT_EQ(events[0]["actor_user_id"].as<std::string>(), admin_.id);
    EXPECT_EQ(events[0]["reason"].as<std::string>(), "张工异常退出，管理员交接");
}

TEST_F(EditLockRepositoryTest, UnknownImportRecordDoesNotAcquire) {
    bridge_report::db::EditLockRepository repository(client_);
    const auto outcome = repository.acquire("00000000-0000-0000-0000-000000000000", owner_);
    EXPECT_FALSE(outcome.acquired);
    EXPECT_FALSE(outcome.import_record_found);
    EXPECT_FALSE(outcome.lock.has_value());
}

TEST_F(EditLockRepositoryTest, TwoIndependentConnectionsRaceAndOnlyOneAcquires) {
    const bridge_report::config::PostgresConfig config{};
    auto first_client = bridge_report::db::create_db_client(config, 1);
    auto second_client = bridge_report::db::create_db_client(config, 1);

    auto first = std::async(std::launch::async, [&] {
        return bridge_report::db::EditLockRepository(first_client).acquire(record_id_, owner_);
    });
    auto second = std::async(std::launch::async, [&] {
        return bridge_report::db::EditLockRepository(second_client).acquire(record_id_, other_);
    });
    const auto first_outcome = first.get();
    const auto second_outcome = second.get();

    EXPECT_NE(first_outcome.acquired, second_outcome.acquired);
    const auto active = bridge_report::db::EditLockRepository(client_).get_active(record_id_);
    ASSERT_TRUE(active.has_value());
    EXPECT_TRUE(active->user_id == owner_.id || active->user_id == other_.id);

    first_client->closeAll();
    second_client->closeAll();
}

TEST_F(EditLockRepositoryTest, DeletingOwningSessionReleasesLockByCascade) {
    bridge_report::db::EditLockRepository repository(client_);
    const auto acquired = repository.acquire(record_id_, owner_);
    ASSERT_TRUE(acquired.acquired);

    client_->execSqlSync("delete from user_sessions where id = $1::uuid", owner_.session_id);

    EXPECT_FALSE(repository.get_active(record_id_).has_value());
    EXPECT_EQ(repository.check(record_id_, owner_, acquired.lock_token).status,
              bridge_report::db::EditLockCheckStatus::required);
    EXPECT_TRUE(repository.acquire(record_id_, other_).acquired);
}

TEST_F(EditLockRepositoryTest, ReopenAndLockAreAtomicAndRestoreReleasesInSameStatement) {
    client_->execSqlSync(
        "update import_records set import_status = '已确认', parsed_result_json = '{\"draft\":1}'::jsonb "
        "where id = $1::uuid",
        record_id_);
    bridge_report::db::EditLockRepository lock_repository(client_);

    const auto reopened = lock_repository.acquire_and_reopen(record_id_, owner_, "warnings_only");
    ASSERT_TRUE(reopened.acquired);
    const auto state = client_->execSqlSync(
        "select import_status, reopen_scope, reopen_backup_parsed_result_json::text as backup "
        "from import_records where id = $1::uuid",
        record_id_);
    ASSERT_EQ(state.size(), 1U);
    EXPECT_EQ(state[0]["import_status"].as<std::string>(), "待校对");
    EXPECT_EQ(state[0]["reopen_scope"].as<std::string>(), "warnings_only");
    EXPECT_FALSE(state[0]["backup"].isNull());

    const bridge_report::db::EditLockCredentials credentials{
        owner_.id, owner_.session_id, reopened.lock_token};
    bridge_report::db::ReviewRepository review_repository(client_);
    EXPECT_TRUE(review_repository.restore_reopened_import_record(record_id_, credentials));
    EXPECT_FALSE(lock_repository.get_active(record_id_).has_value());
    const auto restored = client_->execSqlSync(
        "select import_status, reopened_at is null as reopen_cleared "
        "from import_records where id = $1::uuid",
        record_id_);
    EXPECT_EQ(restored[0]["import_status"].as<std::string>(), "已确认");
    EXPECT_TRUE(restored[0]["reopen_cleared"].as<bool>());
}

TEST_F(EditLockRepositoryTest, SaveRechecksLockAndCancelChangesStateWhileReleasing) {
    bridge_report::db::EditLockRepository lock_repository(client_);
    const auto acquired = lock_repository.acquire(record_id_, owner_);
    ASSERT_TRUE(acquired.acquired);
    bridge_report::db::ReviewRepository review_repository(client_);

    const bridge_report::db::EditLockCredentials wrong{
        other_.id, other_.session_id, acquired.lock_token};
    EXPECT_FALSE(review_repository.save_review_draft(record_id_, "{\"value\":2}", wrong));

    const bridge_report::db::EditLockCredentials correct{
        owner_.id, owner_.session_id, acquired.lock_token};
    EXPECT_TRUE(review_repository.save_review_draft(record_id_, "{\"value\":3}", correct));
    EXPECT_TRUE(lock_repository.get_active(record_id_).has_value());
    EXPECT_TRUE(review_repository.cancel_import_record(record_id_, correct));
    EXPECT_FALSE(lock_repository.get_active(record_id_).has_value());

    const auto state = client_->execSqlSync(
        "select import_status, parsed_result_json::text as parsed from import_records where id = $1::uuid",
        record_id_);
    EXPECT_EQ(state[0]["import_status"].as<std::string>(), "已取消");
    EXPECT_EQ(state[0]["parsed"].as<std::string>(), "{\"value\": 3}");
}

}  // namespace
