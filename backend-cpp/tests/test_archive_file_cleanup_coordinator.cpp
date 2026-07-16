#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/deletion/ArchiveFileCleanupCoordinator.hpp"

namespace {

drogon::orm::DbClientPtr test_client() {
    const auto* url = std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL");
    if (url == nullptr) return nullptr;
    return bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{}, 1);
}

std::filesystem::path make_root(const char* name) {
    auto root = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "queue");
    return root;
}

std::string create_year_audit(const drogon::orm::DbClientPtr& client) {
    return client->execSqlSync(
        "insert into inspection_year_deletion_audits "
        "(bridge_system_number_snapshot,bridge_name_snapshot,inspection_year,"
        "actor_username_snapshot,actor_display_name_snapshot,reason) "
        "values ('QL-TEST','测试桥',2026,'admin','管理员','测试清理') returning id::text"
    )[0]["id"].as<std::string>();
}

std::string create_bridge_audit(const drogon::orm::DbClientPtr& client) {
    return client->execSqlSync(
        "insert into bridge_deletion_audits "
        "(batch_id,bridge_system_number_snapshot,bridge_name_snapshot,status_snapshot,"
        "actor_username_snapshot,actor_display_name_snapshot,reason) "
        "values (gen_random_uuid(),'QL-TEST','测试桥','在用','admin','管理员','测试清理') "
        "returning id::text"
    )[0]["id"].as<std::string>();
}

}  // namespace

TEST(ArchiveFileCleanupCoordinatorTest, ProcessesAnnualAndBridgeQueues) {
    const auto client = test_client();
    if (client == nullptr) GTEST_SKIP();
    const auto root = make_root("bridge_report_cleanup_coordinator_both");
    std::ofstream(root / "queue" / "annual.docx") << "annual";
    std::ofstream(root / "queue" / "bridge.docx") << "bridge";
    const auto year_audit = create_year_audit(client);
    const auto bridge_audit = create_bridge_audit(client);
    client->execSqlSync(
        "insert into archived_file_deletion_queue (deletion_audit_id,storage_relative_path) "
        "values ($1::uuid,'queue/annual.docx')", year_audit);
    client->execSqlSync(
        "insert into bridge_archived_file_deletion_queue "
        "(bridge_deletion_audit_id,storage_relative_path) values ($1::uuid,'queue/bridge.docx')",
        bridge_audit);

    bridge_report::deletion::ArchiveFileCleanupCoordinator coordinator(client, root);
    auto summary = coordinator.process_annual_audit(year_audit);
    summary += coordinator.process_bridge_audit(bridge_audit);

    EXPECT_EQ(summary.claimed, 2);
    EXPECT_EQ(summary.completed, 2);
    EXPECT_EQ(summary.failed, 0);
    EXPECT_FALSE(std::filesystem::exists(root / "queue" / "annual.docx"));
    EXPECT_FALSE(std::filesystem::exists(root / "queue" / "bridge.docx"));
    EXPECT_EQ(client->execSqlSync(
        "select file_cleanup_status from inspection_year_deletion_audits where id=$1::uuid",
        year_audit)[0]["file_cleanup_status"].as<std::string>(), "已完成");
    EXPECT_EQ(client->execSqlSync(
        "select file_cleanup_status from bridge_deletion_audits where id=$1::uuid",
        bridge_audit)[0]["file_cleanup_status"].as<std::string>(), "已完成");

    client->execSqlSync("delete from inspection_year_deletion_audits where id=$1::uuid", year_audit);
    client->execSqlSync("delete from bridge_deletion_audits where id=$1::uuid", bridge_audit);
    std::filesystem::remove_all(root);
}

TEST(ArchiveFileCleanupCoordinatorTest, FailedItemBacksOffThenRetries) {
    const auto client = test_client();
    if (client == nullptr) GTEST_SKIP();
    const auto root = make_root("bridge_report_cleanup_coordinator_retry");
    std::filesystem::create_directories(root / "queue" / "busy");
    std::ofstream(root / "queue" / "busy" / "child.txt") << "busy";
    const auto audit = create_year_audit(client);
    client->execSqlSync(
        "insert into archived_file_deletion_queue (deletion_audit_id,storage_relative_path) "
        "values ($1::uuid,'queue/busy')", audit);

    bridge_report::deletion::ArchiveFileCleanupPolicy policy;
    policy.retry_base_seconds = 60;
    bridge_report::deletion::ArchiveFileCleanupCoordinator coordinator(client, root, policy);
    auto summary = coordinator.process_annual_audit(audit);
    EXPECT_EQ(summary.failed, 1);
    auto row = client->execSqlSync(
        "select status,attempt_count,next_attempt_at>now() as backed_off "
        "from archived_file_deletion_queue where deletion_audit_id=$1::uuid", audit)[0];
    EXPECT_EQ(row["status"].as<std::string>(), "失败待重试");
    EXPECT_EQ(row["attempt_count"].as<int>(), 1);
    EXPECT_TRUE(row["backed_off"].as<bool>());

    std::filesystem::remove(root / "queue" / "busy" / "child.txt");
    client->execSqlSync(
        "update archived_file_deletion_queue set next_attempt_at=now() "
        "where deletion_audit_id=$1::uuid", audit);
    summary = coordinator.process_annual_audit(audit);
    EXPECT_EQ(summary.completed, 1);
    EXPECT_FALSE(std::filesystem::exists(root / "queue" / "busy"));

    client->execSqlSync("delete from inspection_year_deletion_audits where id=$1::uuid", audit);
    std::filesystem::remove_all(root);
}

TEST(ArchiveFileCleanupCoordinatorTest, RecoversStaleProcessingClaim) {
    const auto client = test_client();
    if (client == nullptr) GTEST_SKIP();
    const auto root = make_root("bridge_report_cleanup_coordinator_stale");
    std::ofstream(root / "queue" / "stale.docx") << "stale";
    const auto audit = create_year_audit(client);
    client->execSqlSync(
        "insert into archived_file_deletion_queue "
        "(deletion_audit_id,storage_relative_path,status,processing_started_at) "
        "values ($1::uuid,'queue/stale.docx','清理中',now()-interval '1 hour')", audit);

    bridge_report::deletion::ArchiveFileCleanupPolicy policy;
    policy.claim_timeout_seconds = 1;
    bridge_report::deletion::ArchiveFileCleanupCoordinator coordinator(client, root, policy);
    const auto summary = coordinator.process_annual_audit(audit);

    EXPECT_EQ(summary.claimed, 1);
    EXPECT_EQ(summary.completed, 1);
    client->execSqlSync("delete from inspection_year_deletion_audits where id=$1::uuid", audit);
    std::filesystem::remove_all(root);
}
