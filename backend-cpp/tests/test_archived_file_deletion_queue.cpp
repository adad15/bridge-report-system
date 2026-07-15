#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/deletion/ArchivedFileDeletionQueue.hpp"

namespace {

TEST(ArchivedFileDeletionQueueTest, RemovesSafeFileAndCompletesAudit) {
    if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) GTEST_SKIP();
    const auto client = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{}, 1);
    const auto root = std::filesystem::temp_directory_path() / ("bridge-delete-" + std::to_string(std::rand()));
    std::filesystem::create_directories(root / "nested");
    std::ofstream(root / "nested" / "delete.txt") << "delete me";

    const auto audit_id = client->execSqlSync(
        "insert into inspection_year_deletion_audits(bridge_system_number_snapshot,bridge_name_snapshot,inspection_year,"
        "actor_username_snapshot,actor_display_name_snapshot,reason) values('QL-test','queue-test',2026,'admin','管理员','测试') returning id"
    )[0]["id"].as<std::string>();
    client->execSqlSync(
        "insert into archived_file_deletion_queue(deletion_audit_id,storage_relative_path) values($1::uuid,'nested/delete.txt')",
        audit_id);

    bridge_report::deletion::ArchivedFileDeletionQueue queue(client, root);
    const auto result = queue.process_audit(audit_id);
    EXPECT_EQ(result.completed, 1);
    EXPECT_EQ(result.failed, 0);
    EXPECT_FALSE(std::filesystem::exists(root / "nested" / "delete.txt"));
    EXPECT_EQ(client->execSqlSync("select file_cleanup_status from inspection_year_deletion_audits where id=$1::uuid", audit_id)[0]["file_cleanup_status"].as<std::string>(), "已完成");

    client->execSqlSync("delete from inspection_year_deletion_audits where id=$1::uuid", audit_id);
    std::filesystem::remove_all(root);
    client->closeAll();
}

}  // namespace
