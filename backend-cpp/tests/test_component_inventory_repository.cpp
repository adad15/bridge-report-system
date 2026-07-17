#include <cstdlib>
#include <memory>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/db/DbClientFactory.hpp"

namespace db = bridge_report::db;
namespace inventory = bridge_report::inventory;

class ComponentInventoryRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) return;
        client = bridge_report::db::create_db_client(
            bridge_report::config::PostgresConfig{}, 1);
        user_id = client->execSqlSync(
            "select id::text from users where username='admin'")[0]["id"].as<std::string>();
        package_id = client->execSqlSync(
            "insert into standard_packages(standard_family,standard_id,standard_code,standard_name,"
            "official_edition,package_version,contract_version,algorithm_id,effective_date,content_checksum) "
            "values('technical_condition','INVENTORY-REPO-'||gen_random_uuid()::text,"
            "'INVENTORY REPO','构件台账仓储测试规范','2026','1.0.0',1,'inventory-repo',"
            "'2026-01-01','sha256:'||repeat('a',64)) returning id::text")[0]["id"].as<std::string>();
        other_package_id = client->execSqlSync(
            "insert into standard_packages(standard_family,standard_id,standard_code,standard_name,"
            "official_edition,package_version,contract_version,algorithm_id,effective_date,content_checksum) "
            "values('technical_condition','INVENTORY-REPO-OTHER-'||gen_random_uuid()::text,"
            "'INVENTORY REPO OTHER','另一构件台账测试规范','2026','1.0.0',1,"
            "'inventory-repo-other','2026-01-01','sha256:'||repeat('c',64)) returning id::text")
            [0]["id"].as<std::string>();
        bridge_id = client->execSqlSync(
            "insert into bridges(bridge_name) values('构件台账仓储测试桥') returning id::text")
            [0]["id"].as<std::string>();
    }

    void TearDown() override {
        if (!client) return;
        try {
            client->execSqlSync("delete from defect_observations where bridge_id=$1::uuid", bridge_id);
            client->execSqlSync(
                "update inspection_years set component_inventory_revision_id=null "
                "where bridge_id=$1::uuid", bridge_id);
            client->execSqlSync("delete from inspection_years where bridge_id=$1::uuid", bridge_id);
            client->execSqlSync("delete from bridges where id=$1::uuid", bridge_id);
            client->execSqlSync("delete from standard_packages where id=$1::uuid", package_id);
            client->execSqlSync("delete from standard_packages where id=$1::uuid", other_package_id);
        } catch (...) {
        }
        client->closeAll();
    }

    inventory::GenerateInventoryInput girder_input(int spans, int per_span) const {
        inventory::GenerateInventoryInput input;
        input.standard_package_id = package_id;
        input.template_id = "test.inventory.beam";
        input.bridge_type_id = "test.bridge.beam";
        input.span_count = spans;
        input.input_quantities["span_count"] = spans;
        input.input_quantities["members_per_span"] = per_span;
        input.groups.push_back({
            "主梁", "主梁", "test.component.main_girder", "superstructure",
            inventory::NumberingMode::SpanMember, per_span, "", "#"});
        return input;
    }

    drogon::orm::DbClientPtr client;
    std::string user_id;
    std::string package_id;
    std::string other_package_id;
    std::string bridge_id;
};

TEST_F(ComponentInventoryRepositoryTest, GeneratedComponentsKeepStableIdentityWhenNumberChanges) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(5, 6);
    const auto generated = inventory::generate_component_inventory(input);
    ASSERT_TRUE(generated.ok());

    db::ComponentInventoryRepository repository(client);
    const auto created = repository.generate_draft(
        bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    ASSERT_TRUE(created.revision.has_value());
    ASSERT_EQ(created.revision->entries.size(), 30u);

    std::set<std::string> physical_ids;
    for (const auto& entry : created.revision->entries) {
        physical_ids.insert(entry.bridge_component_id);
    }
    EXPECT_EQ(physical_ids.size(), 30u);

    const auto original = created.revision->entries.front();
    db::InventoryEntryUpdate update;
    update.component_number = "自定义-01";
    update.site_name = original.site_name;
    update.site_component_type = original.site_component_type;
    update.span_or_location = original.span_or_location;
    const auto updated = repository.update_entry(
        created.revision->id, original.id, user_id, update);
    ASSERT_EQ(updated.status, db::ComponentInventoryStatus::Ok);
    ASSERT_TRUE(updated.revision.has_value());
    const auto& updated_entry = updated.revision->entries.front();
    EXPECT_EQ(updated_entry.bridge_component_id, original.bridge_component_id);
    EXPECT_EQ(updated_entry.component_number, "自定义-01");

    db::InventoryNewEntry duplicate;
    duplicate.component_number = "自定义-01";
    duplicate.site_name = "重复主梁";
    duplicate.site_component_type = "主梁";
    EXPECT_EQ(
        repository.add_entry(updated.revision->id, user_id, duplicate).status,
        db::ComponentInventoryStatus::Conflict);

    db::InventoryNewEntry removable;
    removable.component_number = "临时-01";
    removable.site_name = "误生成构件";
    removable.site_component_type = "临时构件";
    const auto added = repository.add_entry(updated.revision->id, user_id, removable);
    ASSERT_EQ(added.status, db::ComponentInventoryStatus::Ok);
    const auto added_entry = *added.entry_id;
    const auto physical_id = client->execSqlSync(
        "select bridge_component_id::text from bridge_component_inventory_entries where id=$1::uuid",
        added_entry)[0]["bridge_component_id"].as<std::string>();
    const auto removed = repository.delete_entry(
        updated.revision->id, added_entry, user_id);
    ASSERT_EQ(removed.status, db::ComponentInventoryStatus::Ok);
    EXPECT_TRUE(client->execSqlSync(
        "select 1 from bridge_components where id=$1::uuid", physical_id).empty());
}

TEST_F(ComponentInventoryRepositoryTest, ConfirmedEditCreatesDraftAndReferencedComponentCanOnlyDeactivate) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_one = created.revision->id;
    const auto entry_one = created.revision->entries.front();

    db::InventoryMappingUpdate mapping;
    mapping.standard_package_id = package_id;
    mapping.standard_bridge_type_id = input.bridge_type_id;
    mapping.standard_component_category_id = "test.component.main_girder";
    mapping.structure_part = "superstructure";
    auto mapped = repository.set_mapping(revision_one, entry_one.id, user_id, mapping);
    ASSERT_EQ(mapped.status, db::ComponentInventoryStatus::Ok);
    mapping.standard_package_id = other_package_id;
    mapping.standard_component_category_id = "other-standard.component.main_girder";
    mapped = repository.set_mapping(revision_one, entry_one.id, user_id, mapping);
    ASSERT_EQ(mapped.status, db::ComponentInventoryStatus::Ok);
    auto confirmed = repository.confirm_revision(revision_one, user_id, "仓储测试确认");
    ASSERT_EQ(confirmed.status, db::ComponentInventoryStatus::Ok);
    EXPECT_EQ(confirmed.revision->status, "已确认");

    db::InventoryEntryUpdate update;
    update.component_number = "修改后编号";
    update.site_name = entry_one.site_name;
    update.site_component_type = entry_one.site_component_type;
    update.span_or_location = entry_one.span_or_location;
    auto edited = repository.update_entry(revision_one, entry_one.id, user_id, update);
    ASSERT_EQ(edited.status, db::ComponentInventoryStatus::Ok);
    ASSERT_EQ(edited.revision->revision_number, 2);
    ASSERT_EQ(edited.revision->status, "草稿");
    ASSERT_EQ(edited.revision->entries.size(), 1u);
    EXPECT_EQ(edited.revision->entries[0].bridge_component_id, entry_one.bridge_component_id);

    const auto year_id = client->execSqlSync(
        "insert into inspection_years(bridge_id,inspection_year,status,"
        "component_inventory_revision_id) values($1::uuid,2026,'已确认',$2::uuid) "
        "returning id::text",
        bridge_id, revision_one)[0]["id"].as<std::string>();
    client->execSqlSync(
        "insert into defect_observations(inspection_year_id,bridge_id,bridge_component_id,"
        "structure_part,defect_type,defect_description_raw) "
        "values($1::uuid,$2::uuid,$3::uuid,'上部结构','裂缝','构件引用测试')",
        year_id, bridge_id, entry_one.bridge_component_id);

    EXPECT_EQ(
        repository.delete_entry(
            edited.revision->id, edited.revision->entries[0].id, user_id).status,
        db::ComponentInventoryStatus::Referenced);
    const auto deactivated = repository.deactivate_entry(
        edited.revision->id, edited.revision->entries[0].id, user_id, "现场已停用");
    ASSERT_EQ(deactivated.status, db::ComponentInventoryStatus::Ok);
    EXPECT_FALSE(deactivated.revision->entries[0].is_active);
    EXPECT_EQ(deactivated.revision->entries[0].deactivation_reason, "现场已停用");
}

TEST_F(ComponentInventoryRepositoryTest, ConfirmationReportsUnmappedManualEntry) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);

    db::InventoryNewEntry manual;
    manual.component_number = "Z-1";
    manual.site_name = "自定义现场构件";
    manual.site_component_type = "自定义类型";
    auto added = repository.add_entry(created.revision->id, user_id, manual);
    ASSERT_EQ(added.status, db::ComponentInventoryStatus::Ok);
    const auto confirmation = repository.confirm_revision(
        created.revision->id, user_id, "应被阻断");
    ASSERT_EQ(confirmation.status, db::ComponentInventoryStatus::Blocked);
    ASSERT_FALSE(confirmation.blockers.empty());
    EXPECT_EQ(confirmation.blockers.front().code, "component_mapping_required");
}
