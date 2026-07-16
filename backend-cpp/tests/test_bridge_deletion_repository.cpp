#include <cstdlib>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/BridgeDeletionRepository.hpp"
#include "bridge_report/db/DbClientFactory.hpp"

TEST(BridgeDeletionRepositoryTest, DeletesBridgeAndRetainsAuditSnapshot) {
    if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) GTEST_SKIP();
    const auto client = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{}, 1);
    const auto bridge_id = client->execSqlSync(
        "insert into bridges(bridge_name) values('整桥删除仓储测试') returning id::text as id")[0]["id"].as<std::string>();
    client->execSqlSync(
        "insert into inspection_years(bridge_id,inspection_year) values($1::uuid,2026)", bridge_id);
    bridge_report::db::BridgeDeletionRepository repository(client);
    const auto preview = repository.preview(bridge_id);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->counts.inspection_versions, 1);
    bridge_report::deletion::DeletionActorSnapshot actor;
    actor.user_id = client->execSqlSync("select id::text as id from users where username='admin'")[0]["id"].as<std::string>();
    actor.username = "admin";
    actor.display_name = "管理员";
    const auto batch_id = client->execSqlSync("select gen_random_uuid()::text as id")[0]["id"].as<std::string>();
    const auto outcome = repository.delete_bridge(
        bridge_id, preview->impact_token(), "误建桥梁", actor, batch_id);
    ASSERT_EQ(outcome.status, bridge_report::deletion::DeleteBridgeStatus::Deleted);
    EXPECT_TRUE(client->execSqlSync("select 1 from bridges where id=$1::uuid", bridge_id).empty());
    const auto audit = client->execSqlSync(
        "select bridge_id,bridge_name_snapshot from bridge_deletion_audits where id=$1::uuid",
        *outcome.deletion_audit_id)[0];
    EXPECT_TRUE(audit["bridge_id"].isNull());
    EXPECT_EQ(audit["bridge_name_snapshot"].as<std::string>(), "整桥删除仓储测试");
    client->execSqlSync("delete from bridge_deletion_audits where id=$1::uuid", *outcome.deletion_audit_id);
    client->closeAll();
}
