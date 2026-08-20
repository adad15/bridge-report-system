#include <chrono>
#include <cstdlib>
#include <thread>

#include <drogon/orm/Exception.h>
#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/BridgeDeletionRepository.hpp"
#include "bridge_report/db/DbClientFactory.hpp"

TEST(BridgeDeletionRepositoryTest, DeletesBridgeAndRetainsAuditSnapshot) {
    if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) GTEST_SKIP();
    const auto client = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{}, 1);
    const auto bridge_id = client->execSqlSync(
        "insert into bridges(bridge_name) values('整桥删除仓储测试') returning id::text as id")[0]["id"].as<std::string>();
    const auto actor_user_id = client->execSqlSync(
        "select id::text as id from users where username='admin'")[0]["id"].as<std::string>();
    const auto inventory_package_id = client->execSqlSync(
        "insert into standard_packages(standard_family,standard_id,standard_code,standard_name,"
        "official_edition,package_version,contract_version,algorithm_id,effective_date,content_checksum) "
        "values('technical_condition','DELETE-INVENTORY-'||gen_random_uuid()::text,"
        "'DELETE INVENTORY','整桥删除台账测试规范','2026','1.0.0',1,'delete-inventory',"
        "'2026-01-01','sha256:'||repeat('b',64)) returning id::text as id")
        [0]["id"].as<std::string>();
    const auto batch_id_for_inventory = client->execSqlSync(
        "insert into bridge_component_generation_batches(bridge_id,template_standard_package_id,"
        "template_id,bridge_type_code,input_quantities,generated_by_user_id) "
        "values($1::uuid,$2::uuid,'delete.template','delete.bridge','{}'::jsonb,$3::uuid) "
        "returning id::text as id",
        bridge_id, inventory_package_id, actor_user_id)[0]["id"].as<std::string>();
    const auto inventory_revision_id = client->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,created_by_user_id) "
        "values($1::uuid,1,$2::uuid) returning id::text as id",
        bridge_id, actor_user_id)[0]["id"].as<std::string>();
    const auto component_id = client->execSqlSync(
        "insert into bridge_components(bridge_id,structure_part,component_type,business_component_code,"
        "normalized_component_key,creation_source) "
        "values($1::uuid,'上部结构','主梁','1-1#','delete-inventory-component','人工录入') "
        "returning id::text as id",
        bridge_id)[0]["id"].as<std::string>();
    const auto inventory_entry_id = client->execSqlSync(
        "insert into bridge_component_inventory_entries(inventory_revision_id,bridge_component_id,"
        "generation_batch_id,component_number,site_name,site_component_type) "
        "values($1::uuid,$2::uuid,$3::uuid,'1-1#','主梁','主梁') returning id::text as id",
        inventory_revision_id, component_id, batch_id_for_inventory)[0]["id"].as<std::string>();
    client->execSqlSync(
        "insert into bridge_component_standard_mappings(inventory_entry_id,standard_package_id,"
        "standard_bridge_type_id,standard_component_category_id,structure_part,mapping_source) "
        "values($1::uuid,$2::uuid,'delete.bridge','delete.component','superstructure','模板生成')",
        inventory_entry_id, inventory_package_id);
    client->execSqlSync(
        "update bridge_component_inventory_revisions set status='已确认',"
        "confirmed_by_user_id=$1::uuid,confirmed_at=now() where id=$2::uuid",
        actor_user_id, inventory_revision_id);
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
    EXPECT_EQ(preview->counts.component_generation_batches, 1);
    EXPECT_EQ(preview->counts.component_inventory_revisions, 1);
    EXPECT_EQ(preview->counts.component_inventory_entries, 1);
    EXPECT_EQ(preview->counts.component_standard_mappings, 1);
    bridge_report::deletion::DeletionActorSnapshot actor;
    actor.user_id = actor_user_id;
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
    client->execSqlSync("delete from standard_packages where id=$1::uuid", inventory_package_id);
    client->closeAll();
}


// 与 InspectionYearDeletionRepositoryTest.DeletionLocksImportRecordsBeforeTheYear 同一条
// 不变量、同一套判据，守的是另一个文件：整桥删除也曾是 inspection_years 在前。
// 两处各自独立，改了一处不会让另一处的测试转红，所以两条都要有。
TEST(BridgeDeletionRepositoryTest, DeletionLocksImportRecordsBeforeTheYear) {
    if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) GTEST_SKIP();
    const auto client = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{}, 1);
    const auto bridge_id = client->execSqlSync(
        "insert into bridges(bridge_name) values('整桥删除锁顺序测试') returning id::text as id")
        [0]["id"].as<std::string>();
    const auto actor_user_id = client->execSqlSync(
        "select id::text as id from users where username='admin'")[0]["id"].as<std::string>();
    const auto year_id = client->execSqlSync(
        "insert into inspection_years(bridge_id,inspection_year,status) "
        "values($1::uuid,2031,'待校对') returning id::text as id", bridge_id)[0]["id"].as<std::string>();
    const auto import_id = client->execSqlSync(
        "insert into import_records(bridge_id,inspection_year_id,import_name,source_type) "
        "values($1::uuid,$2::uuid,'锁顺序测试导入','正式Word') returning id::text as id",
        bridge_id, year_id)[0]["id"].as<std::string>();

    bridge_report::db::BridgeDeletionRepository repository(client);
    const auto preview = repository.preview(bridge_id);
    ASSERT_TRUE(preview.has_value());
    const auto token = preview->impact_token();
    bridge_report::deletion::DeletionActorSnapshot actor;
    actor.user_id = actor_user_id;
    actor.username = "admin";
    actor.display_name = "管理员";
    const auto batch_id = client->execSqlSync(
        "select gen_random_uuid()::text as id")[0]["id"].as<std::string>();

    auto blocker_client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);
    auto blocker = blocker_client->newTransaction();
    blocker->execSqlSync(
        "select id from import_records where id=$1::uuid for update", import_id);

    auto deleter_client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);
    deleter_client->execSqlSync("set statement_timeout = '4000'");
    std::thread deleter([&] {
        bridge_report::db::BridgeDeletionRepository(deleter_client)
            .delete_bridge(bridge_id, token, "锁顺序测试", actor, batch_id);
    });

    auto probe_client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);
    bool deleter_blocked = false;
    for (int attempt = 0; attempt < 200 && !deleter_blocked; ++attempt) {
        deleter_blocked = probe_client->execSqlSync(
            "select count(*) as n from pg_stat_activity "
            "where wait_event_type='Lock' and datname=current_database()"
        )[0]["n"].as<int>() > 0;
        if (!deleter_blocked) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(deleter_blocked) << "删除线程没有被导入记录的行锁挡住，本条的前提不成立";

    bool year_row_free = true;
    try {
        auto probe = probe_client->newTransaction();
        probe->execSqlSync(
            "select id from inspection_years where id=$1::uuid for update nowait", year_id);
        probe->rollback();
    } catch (const drogon::orm::DrogonDbException&) {
        year_row_free = false;
    }

    blocker->rollback();
    deleter.join();
    blocker_client->closeAll();
    deleter_client->closeAll();
    probe_client->closeAll();

    EXPECT_TRUE(year_row_free)
        << "整桥删除在拿到 import_records 之前就锁了 inspection_years，与写路径顺序相反";

    client->execSqlSync("delete from import_records where bridge_id=$1::uuid", bridge_id);
    client->execSqlSync("delete from inspection_years where bridge_id=$1::uuid", bridge_id);
    client->execSqlSync("delete from bridges where id=$1::uuid", bridge_id);
    client->closeAll();
}
