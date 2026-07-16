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
    const auto year_id = client->execSqlSync(
        "insert into inspection_years(bridge_id,inspection_year) values($1::uuid,2026) returning id::text as id",
        bridge_id)[0]["id"].as<std::string>();
    const auto import_id = client->execSqlSync(
        "insert into import_records(bridge_id,inspection_year_id,import_name,source_type,import_status) "
        "values($1::uuid,$2::uuid,'整桥删除临时报告.docx','正式Word','解析失败') returning id::text as id",
        bridge_id, year_id)[0]["id"].as<std::string>();
    const auto source_id = client->execSqlSync(
        "with source_id as(select gen_random_uuid() id) "
        "insert into import_source_files(id,import_record_id,original_file_name,storage_relative_path,"
        "file_extension,file_size_bytes,file_hash,status) "
        "select id,$1::uuid,'整桥删除临时报告.docx',id::text||'.docx','.docx',4,$2,'解析失败' "
        "from source_id returning id::text as id",
        import_id, std::string(64, 'd'))[0]["id"].as<std::string>();
    bridge_report::db::BridgeDeletionRepository repository(client);
    const auto preview = repository.preview(bridge_id);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->counts.inspection_versions, 1);
    EXPECT_EQ(preview->counts.temporary_source_files_to_delete, 1);
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
    const auto source = client->execSqlSync(
        "select import_record_id,status,cleanup_reason from import_source_files where id=$1::uuid", source_id);
    ASSERT_EQ(source.size(), 1u);
    EXPECT_TRUE(source[0]["import_record_id"].isNull());
    EXPECT_EQ(source[0]["status"].as<std::string>(), "待清理");
    EXPECT_EQ(source[0]["cleanup_reason"].as<std::string>(), "业务删除");
    client->execSqlSync("delete from import_source_files where id=$1::uuid", source_id);
    client->execSqlSync("delete from bridge_deletion_audits where id=$1::uuid", *outcome.deletion_audit_id);
    client->closeAll();
}
