#include <cstdlib>
#include <fstream>
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
        input.bridge_type_id = "test.bridge.beam";
        input.span_count = spans;
        input.part_selections.push_back({"beam.girder", "主梁", {per_span}});
        return input;
    }

    drogon::orm::DbClientPtr client;
    std::string user_id;
    std::string package_id;
    std::string other_package_id;
    std::string bridge_id;
};

namespace {

// 除 generate_draft 外的写方法不再回传全量修订版；修订版 id 从汇总里取。
std::string revision_id_of(const db::ComponentInventoryOutcome& outcome) {
    return (*outcome.summary)["revision"]["id"].asString();
}

}  // namespace

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
    ASSERT_TRUE(updated.summary.has_value());
    ASSERT_TRUE(updated.entry.has_value());
    const auto& updated_entry = updated.entry->entry;
    EXPECT_EQ(updated_entry.bridge_component_id, original.bridge_component_id);
    EXPECT_EQ(updated_entry.component_number, "自定义-01");

    db::InventoryNewEntry duplicate;
    duplicate.component_number = "自定义-01";
    duplicate.site_name = "重复主梁";
    duplicate.site_component_type = "主梁";
    EXPECT_EQ(
        repository.add_entry(revision_id_of(updated), user_id, duplicate).status,
        db::ComponentInventoryStatus::Conflict);

    db::InventoryNewEntry removable;
    removable.component_number = "临时-01";
    removable.site_name = "误生成构件";
    removable.site_component_type = "临时构件";
    const auto added = repository.add_entry(revision_id_of(updated), user_id, removable);
    ASSERT_EQ(added.status, db::ComponentInventoryStatus::Ok);
    const auto added_entry = *added.entry_id;
    const auto physical_id = client->execSqlSync(
        "select bridge_component_id::text from bridge_component_inventory_entries where id=$1::uuid",
        added_entry)[0]["bridge_component_id"].as<std::string>();
    const auto removed = repository.delete_entry(
        revision_id_of(updated), added_entry, user_id);
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
    EXPECT_EQ((*confirmed.summary)["revision"]["status"].asString(), "已确认");

    db::InventoryEntryUpdate update;
    update.component_number = "修改后编号";
    update.site_name = entry_one.site_name;
    update.site_component_type = entry_one.site_component_type;
    update.span_or_location = entry_one.span_or_location;
    auto edited = repository.update_entry(revision_one, entry_one.id, user_id, update);
    ASSERT_EQ(edited.status, db::ComponentInventoryStatus::Ok);
    ASSERT_TRUE(edited.summary.has_value());
    const auto& edited_revision = (*edited.summary)["revision"];
    ASSERT_EQ(edited_revision["revision_number"].asInt(), 2);
    ASSERT_EQ(edited_revision["status"].asString(), "草稿");
    ASSERT_EQ(edited_revision["active_entry_count"].asInt(), 1);
    ASSERT_TRUE(edited.entry.has_value());
    EXPECT_EQ(edited.entry->entry.bridge_component_id, entry_one.bridge_component_id);

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
            revision_id_of(edited), edited.entry->entry.id, user_id).status,
        db::ComponentInventoryStatus::Referenced);
    const auto deactivated = repository.deactivate_entry(
        revision_id_of(edited), edited.entry->entry.id, user_id, "现场已停用");
    ASSERT_EQ(deactivated.status, db::ComponentInventoryStatus::Ok);
    ASSERT_TRUE(deactivated.entry.has_value());
    EXPECT_FALSE(deactivated.entry->entry.is_active);
    EXPECT_EQ(deactivated.entry->entry.deactivation_reason, "现场已停用");
}

TEST_F(ComponentInventoryRepositoryTest, GeneratedMappingsAreConfirmedByGeneratingUser) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(2, 3);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    const auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    ASSERT_EQ(created.revision->entries.size(), 6u);
    for (const auto& entry : created.revision->entries) {
        ASSERT_EQ(entry.mappings.size(), 1u);
        EXPECT_EQ(entry.mappings[0].confirmation_status, "已确认");
        EXPECT_EQ(entry.mappings[0].mapping_source, "模板生成");
    }
    // 生成后的台账不再有映射阻塞，可直接确认。
    EXPECT_EQ(
        repository.confirm_revision(created.revision->id, user_id, "生成即确认").status,
        db::ComponentInventoryStatus::Ok);
}

TEST_F(ComponentInventoryRepositoryTest, PendingMappingsCanBeConfirmedInBatch) {
    if (!client) GTEST_SKIP();
    auto input = girder_input(1, 2);
    input.part_selections.push_back({"beam.diaphragm", "横隔板", {1, 2}});
    const auto generated = inventory::generate_component_inventory(input);
    ASSERT_TRUE(generated.ok());
    db::ComponentInventoryRepository repository(client);
    const auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = created.revision->id;

    // 模拟旧数据：全部映射退回待确认。
    client->execSqlSync(
        "update bridge_component_standard_mappings m set confirmation_status='待确认',"
        "confirmed_by_user_id=null,confirmed_at=null "
        "from bridge_component_inventory_entries e "
        "where m.inventory_entry_id=e.id and e.inventory_revision_id=$1::uuid",
        revision_id);

    // 按构件类别只确认横隔板。
    auto partial = repository.confirm_pending_mappings(revision_id, user_id, "横隔板");
    ASSERT_EQ(partial.status, db::ComponentInventoryStatus::Ok);
    // 写响应不再带全量构件；同样的意图由分组计数表达：只有横隔板整组转成已确认。
    ASSERT_TRUE(partial.summary.has_value());
    for (const auto& group : (*partial.summary)["groups"]) {
        const bool targeted = group["site_component_type"].asString() == "横隔板";
        const auto active = group["active_count"].asInt();
        EXPECT_EQ(group["confirmed_count"].asInt(), targeted ? active : 0)
            << group["site_component_type"].asString();
        EXPECT_EQ(group["pending_count"].asInt(), targeted ? 0 : active)
            << group["site_component_type"].asString();
    }

    // 不带筛选时确认全部剩余映射。
    auto all = repository.confirm_pending_mappings(revision_id, user_id, "");
    ASSERT_EQ(all.status, db::ComponentInventoryStatus::Ok);
    ASSERT_TRUE(all.summary.has_value());
    for (const auto& group : (*all.summary)["groups"]) {
        EXPECT_EQ(group["pending_count"].asInt(), 0);
        EXPECT_EQ(group["confirmed_count"].asInt(), group["active_count"].asInt());
    }

    // 已确认版本不允许再批量确认映射。
    ASSERT_EQ(
        repository.confirm_revision(revision_id, user_id, "批量确认后定稿").status,
        db::ComponentInventoryStatus::Ok);
    EXPECT_EQ(
        repository.confirm_pending_mappings(revision_id, user_id, "").status,
        db::ComponentInventoryStatus::Conflict);

    EXPECT_EQ(
        repository.confirm_pending_mappings(
            "00000000-0000-0000-0000-000000000000", user_id, "").status,
        db::ComponentInventoryStatus::NotFound);
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

// 多生效映射：唯一索引是 (inventory_entry_id, standard_package_id) where is_active，
// 一个构件可以按规范包挂多个生效映射。判定必须按"存在任一已确认生效映射"，
// 而不是 join + count——后者会把这类构件展开成多行。
TEST_F(ComponentInventoryRepositoryTest, ConfirmationAcceptsEntryWithOnePendingAndOneConfirmedMapping) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto entry = created.revision->entries.front();

    db::InventoryMappingUpdate mapping;
    mapping.standard_package_id = package_id;
    mapping.standard_bridge_type_id = input.bridge_type_id;
    mapping.standard_component_category_id = "test.component.main_girder";
    mapping.structure_part = "superstructure";
    ASSERT_EQ(repository.set_mapping(created.revision->id, entry.id, user_id, mapping).status,
              db::ComponentInventoryStatus::Ok);

    mapping.standard_package_id = other_package_id;
    mapping.standard_component_category_id = "other-standard.component.main_girder";
    ASSERT_EQ(repository.set_mapping(created.revision->id, entry.id, user_id, mapping).status,
              db::ComponentInventoryStatus::Ok);

    // 把第二个包的映射改回待确认，构件于是同时挂着 [已确认(A)、待确认(B)]。
    client->execSqlSync(
        "update bridge_component_standard_mappings set confirmation_status='待确认',"
        "confirmed_by_user_id=null,confirmed_at=null "
        "where inventory_entry_id=$1::uuid and standard_package_id=$2::uuid and is_active",
        entry.id, other_package_id);

    const auto confirmation = repository.confirm_revision(
        created.revision->id, user_id, "任一已确认即可");
    EXPECT_EQ(confirmation.status, db::ComponentInventoryStatus::Ok)
        << "存在任一已确认生效映射就应放行，不该因为另一个包还待确认而被拦";
}

// blocker 规则只有一份来源（blocker_cte_sql 产出的 CTE 文本）。这条用例钉住
// "汇总侧看到的问题"与"confirm 实际拦截的问题"是同一套判定——将来谁把两边拆成
// 两份规则，它就会红。
TEST_F(ComponentInventoryRepositoryTest, BlockerRuleAgreesBetweenInspectionAndConfirmation) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);

    // 模板生成的构件在 generate_draft 里已经带上"模板生成 / 已确认"的映射，
    // 缺映射的只会是手工新增的构件——这两个应当同时出现在两侧。
    std::set<std::string> expected;
    for (const auto* number : {"Z-1", "Z-2"}) {
        db::InventoryNewEntry manual;
        manual.component_number = number;
        manual.site_name = std::string("自定义现场构件") + number;
        manual.site_component_type = "自定义类型";
        const auto added = repository.add_entry(created.revision->id, user_id, manual);
        ASSERT_EQ(added.status, db::ComponentInventoryStatus::Ok);
        ASSERT_TRUE(added.entry.has_value());
        expected.insert(added.entry->entry.id);
    }
    ASSERT_EQ(expected.size(), 2u);

    const auto confirmation = repository.confirm_revision(created.revision->id, user_id, "应被阻断");
    ASSERT_EQ(confirmation.status, db::ComponentInventoryStatus::Blocked);

    std::set<std::string> blocked_ids;
    for (const auto& blocker : confirmation.blockers) {
        EXPECT_EQ(blocker.code, "component_mapping_required");
        blocked_ids.insert(blocker.entity_id);
    }
    EXPECT_EQ(blocked_ids, expected);

    // 直接跑规则片段，结果集必须与 confirm 报出的构件集合完全一致。
    const auto inspected = client->execSqlSync(
        "with inventory_active_entries as ("
        "select count(*)::int as value from bridge_component_inventory_entries "
        "where inventory_revision_id=$1::uuid and is_active),"
        "inventory_unconfirmed_entries as ("
        "select e.id::text as entry_id from bridge_component_inventory_entries e "
        "where e.inventory_revision_id=$1::uuid and e.is_active "
        "and not exists(select 1 from bridge_component_standard_mappings m "
        "where m.inventory_entry_id=e.id and m.is_active "
        "and m.confirmation_status='已确认')) "
        "select entry_id from inventory_unconfirmed_entries",
        created.revision->id);
    std::set<std::string> inspected_ids;
    for (const auto& row : inspected) inspected_ids.insert(row["entry_id"].as<std::string>());
    EXPECT_EQ(inspected_ids, blocked_ids);
}

// ---------------------------------------------------------------------------
// 每桥至多一条草稿。派生路径原先按 baseline_revision_id 查找现有草稿，而
// generate_draft() 见到任何草稿就返回 Conflict——两条路径对同一个不变量的假设相反，
// 数据库也没有约束保证它，于是一桥可以长出两条草稿分支。
// ---------------------------------------------------------------------------

TEST_F(ComponentInventoryRepositoryTest, SingleDraftIndexRejectsSecondDraft) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    const auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);

    // 绕过仓储直接插第二条草稿，验证约束本身在数据库层生效，而不是只靠代码自觉。
    EXPECT_THROW(
        client->execSqlSync(
            "insert into bridge_component_inventory_revisions"
            "(bridge_id,revision_number,created_by_user_id) values($1::uuid,999,$2::uuid)",
            bridge_id, user_id),
        drogon::orm::DrogonDbException);

    const auto drafts = client->execSqlSync(
        "select count(*)::int as count from bridge_component_inventory_revisions "
        "where bridge_id=$1::uuid and status='草稿'",
        bridge_id);
    EXPECT_EQ(drafts[0]["count"].as<int>(), 1);
}

TEST_F(ComponentInventoryRepositoryTest, WritingToSupersededConfirmedRevisionIsRejected) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);

    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto first_entry = created.revision->entries.front();

    db::InventoryMappingUpdate mapping;
    mapping.standard_package_id = package_id;
    mapping.standard_bridge_type_id = input.bridge_type_id;
    mapping.standard_component_category_id = "test.component.main_girder";
    mapping.structure_part = "superstructure";
    ASSERT_EQ(repository.set_mapping(created.revision->id, first_entry.id, user_id, mapping).status,
              db::ComponentInventoryStatus::Ok);
    const auto revision_one = repository.confirm_revision(created.revision->id, user_id, "第一版");
    ASSERT_EQ(revision_one.status, db::ComponentInventoryStatus::Ok);
    const auto revision_one_id = revision_id_of(revision_one);

    // 对已确认版本写入会派生草稿，确认它得到第二个已确认版本。
    db::InventoryEntryUpdate update;
    update.component_number = "派生-01";
    update.site_name = first_entry.site_name;
    update.site_component_type = first_entry.site_component_type;
    update.span_or_location = first_entry.span_or_location;
    auto derived = repository.update_entry(revision_one_id, first_entry.id, user_id, update);
    ASSERT_EQ(derived.status, db::ComponentInventoryStatus::Ok);
    ASSERT_NE(revision_id_of(derived), revision_one_id) << "写入已确认版本应派生出新草稿";
    const auto revision_two = repository.confirm_revision(revision_id_of(derived), user_id, "第二版");
    ASSERT_EQ(revision_two.status, db::ComponentInventoryStatus::Ok);

    // 此时再拿第一版进来：它已不是最新已确认版本，不能再派生第二条分支。
    update.component_number = "回到旧版-01";
    const auto stale = repository.update_entry(
        revision_one_id, first_entry.id, user_id, update);
    EXPECT_EQ(stale.status, db::ComponentInventoryStatus::Superseded);

    const auto drafts = client->execSqlSync(
        "select count(*)::int as count from bridge_component_inventory_revisions "
        "where bridge_id=$1::uuid and status='草稿'",
        bridge_id);
    EXPECT_EQ(drafts[0]["count"].as<int>(), 0) << "被拒绝的写入不应留下草稿";
}

TEST_F(ComponentInventoryRepositoryTest, RepeatedWritesToConfirmedRevisionReuseOneDraft) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 2);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);

    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto entries = created.revision->entries;
    ASSERT_EQ(entries.size(), 2u);

    db::InventoryMappingUpdate mapping;
    mapping.standard_package_id = package_id;
    mapping.standard_bridge_type_id = input.bridge_type_id;
    mapping.standard_component_category_id = "test.component.main_girder";
    mapping.structure_part = "superstructure";
    for (const auto& entry : entries) {
        ASSERT_EQ(repository.set_mapping(created.revision->id, entry.id, user_id, mapping).status,
                  db::ComponentInventoryStatus::Ok);
    }
    const auto confirmed = repository.confirm_revision(created.revision->id, user_id, "基线版");
    ASSERT_EQ(confirmed.status, db::ComponentInventoryStatus::Ok);
    const auto baseline_id = revision_id_of(confirmed);

    // 连着两次写同一个已确认版本：第二次必须落在第一次派生出的那条草稿上。
    db::InventoryEntryUpdate update;
    update.site_name = entries[0].site_name;
    update.site_component_type = entries[0].site_component_type;
    update.span_or_location = entries[0].span_or_location;
    update.component_number = "改-A";
    const auto first_write = repository.update_entry(baseline_id, entries[0].id, user_id, update);
    ASSERT_EQ(first_write.status, db::ComponentInventoryStatus::Ok);

    update.site_name = entries[1].site_name;
    update.site_component_type = entries[1].site_component_type;
    update.span_or_location = entries[1].span_or_location;
    update.component_number = "改-B";
    const auto second_write = repository.update_entry(baseline_id, entries[1].id, user_id, update);
    ASSERT_EQ(second_write.status, db::ComponentInventoryStatus::Ok);
    EXPECT_EQ(revision_id_of(second_write), revision_id_of(first_write));

    const auto drafts = client->execSqlSync(
        "select count(*)::int as count from bridge_component_inventory_revisions "
        "where bridge_id=$1::uuid and status='草稿'",
        bridge_id);
    EXPECT_EQ(drafts[0]["count"].as<int>(), 1);
}

// ---------------------------------------------------------------------------
// 分组汇总。这些用例覆盖的情形在现网真实数据上一个都不出现（每构件仅 1 个生效映射、
// 无停用构件、sort_order 不重复），所以真实数据比对不能替代它们——一个把 count(*)
// 写错的实现照样能在比对里全绿。
// ---------------------------------------------------------------------------

namespace {

Json::Value group_of(const Json::Value& summary, const std::string& type) {
    for (const auto& group : summary["groups"]) {
        if (group["site_component_type"].asString() == type) return group;
    }
    return Json::Value(Json::nullValue);
}

}  // namespace

TEST_F(ComponentInventoryRepositoryTest, SummaryNumberRangeUsesTraversalOrderNotLexicographic) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = created.revision->id;

    // 33 孔的桥：字典序下 '9-2-9#支座' > '33-2-50#支座'，用 min/max 会把范围末端
    // 取到第 9 孔去。
    int order = 10;
    for (const auto* number : {"1-1-1#支座", "9-2-9#支座", "33-2-50#支座"}) {
        db::InventoryNewEntry manual;
        manual.component_number = number;
        manual.site_name = "支座";
        manual.site_component_type = "支座";
        manual.sort_order = order;
        order += 10;
        ASSERT_EQ(repository.add_entry(revision_id, user_id, manual).status,
                  db::ComponentInventoryStatus::Ok);
    }

    const auto summary = repository.load_summary(revision_id);
    ASSERT_TRUE(summary.has_value());
    const auto bearings = group_of(*summary, "支座");
    ASSERT_FALSE(bearings.isNull());
    EXPECT_EQ(bearings["first_number"].asString(), "1-1-1#支座");
    EXPECT_EQ(bearings["last_number"].asString(), "33-2-50#支座");
    EXPECT_EQ(bearings["active_count"].asInt(), 3);
}

TEST_F(ComponentInventoryRepositoryTest, SummaryRangeCountsOnlyActiveEntriesAndKeepsEmptyGroup) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = created.revision->id;

    std::vector<std::string> cone_ids;
    int order = 10;
    for (const auto* number : {"0#台左侧锥坡", "0#台右侧锥坡", "33#台右侧锥坡"}) {
        db::InventoryNewEntry manual;
        manual.component_number = number;
        manual.site_name = "锥坡";
        manual.site_component_type = "锥坡";
        manual.sort_order = order;
        order += 10;
        const auto added = repository.add_entry(revision_id, user_id, manual);
        ASSERT_EQ(added.status, db::ComponentInventoryStatus::Ok);
        ASSERT_TRUE(added.entry.has_value());
        cone_ids.push_back(added.entry->entry.id);
    }
    ASSERT_EQ(cone_ids.size(), 3u);

    // 停用末尾那条：范围要跟着收，不能出现"数量 2、范围到 33#"这种数量与范围打架。
    ASSERT_EQ(repository.deactivate_entry(revision_id, cone_ids.back(), user_id, "现场已拆除").status,
              db::ComponentInventoryStatus::Ok);
    auto summary = repository.load_summary(revision_id);
    ASSERT_TRUE(summary.has_value());
    auto cones = group_of(*summary, "锥坡");
    ASSERT_FALSE(cones.isNull());
    EXPECT_EQ(cones["active_count"].asInt(), 2);
    EXPECT_EQ(cones["first_number"].asString(), "0#台左侧锥坡");
    EXPECT_EQ(cones["last_number"].asString(), "0#台右侧锥坡");

    // 整组停完：该组仍要出现在汇总里，否则界面上这组会凭空消失。
    for (size_t index = 0; index + 1 < cone_ids.size(); ++index) {
        ASSERT_EQ(repository.deactivate_entry(revision_id, cone_ids[index], user_id, "现场已拆除").status,
                  db::ComponentInventoryStatus::Ok);
    }
    summary = repository.load_summary(revision_id);
    ASSERT_TRUE(summary.has_value());
    cones = group_of(*summary, "锥坡");
    ASSERT_FALSE(cones.isNull()) << "整组停用后该组仍应返回";
    EXPECT_EQ(cones["active_count"].asInt(), 0);
    EXPECT_TRUE(cones["first_number"].isNull());
    EXPECT_TRUE(cones["last_number"].isNull());
    EXPECT_EQ(cones["structure_part"].asString(), "other") << "structure_part 非空，缺省为 other";
}

TEST_F(ComponentInventoryRepositoryTest, SummaryDoesNotInflateCountsForMultiPackageMappings) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = created.revision->id;
    const auto entry = created.revision->entries.front();

    // 第二个规范包的生效映射。唯一索引按 (构件, 规范包)，两条可以并存。
    db::InventoryMappingUpdate mapping;
    mapping.standard_package_id = other_package_id;
    mapping.standard_bridge_type_id = input.bridge_type_id;
    mapping.standard_component_category_id = "other-standard.component.main_girder";
    mapping.structure_part = "superstructure";
    ASSERT_EQ(repository.set_mapping(revision_id, entry.id, user_id, mapping).status,
              db::ComponentInventoryStatus::Ok);
    client->execSqlSync(
        "update bridge_component_standard_mappings set confirmation_status='待确认',"
        "confirmed_by_user_id=null,confirmed_at=null "
        "where inventory_entry_id=$1::uuid and standard_package_id=$2::uuid and is_active",
        entry.id, other_package_id);

    const auto summary = repository.load_summary(revision_id);
    ASSERT_TRUE(summary.has_value());
    const auto girders = group_of(*summary, entry.site_component_type);
    ASSERT_FALSE(girders.isNull());
    EXPECT_EQ(girders["active_count"].asInt(), 1) << "两个生效映射不该把构件数翻倍";
    // some 口径：存在任一已确认生效映射即算已确认，不因另一个包还待确认而落进待确认。
    EXPECT_EQ(girders["confirmed_count"].asInt(), 1);
    EXPECT_EQ(girders["pending_count"].asInt(), 0);
    EXPECT_EQ(girders["unmapped_count"].asInt(), 0);
    EXPECT_EQ(girders["confirmed_count"].asInt() + girders["pending_count"].asInt() +
                  girders["unmapped_count"].asInt(),
              girders["active_count"].asInt())
        << "三分必须互斥且覆盖全部启用构件";
    EXPECT_EQ((*summary)["blockers"]["total"].asInt(), 0);
}

TEST_F(ComponentInventoryRepositoryTest, SummaryBlockerCountsSeparatePendingFromUnmapped) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = created.revision->id;
    const auto generated_entry = created.revision->entries.front();

    // 一个完全没有映射的手工构件，外加把生成构件的映射改成待确认。
    db::InventoryNewEntry manual;
    manual.component_number = "Z-1";
    manual.site_name = "自定义现场构件";
    manual.site_component_type = "自定义类型";
    manual.sort_order = 500;
    ASSERT_EQ(repository.add_entry(revision_id, user_id, manual).status,
              db::ComponentInventoryStatus::Ok);

    client->execSqlSync(
        "update bridge_component_standard_mappings set confirmation_status='待确认',"
        "confirmed_by_user_id=null,confirmed_at=null "
        "where inventory_entry_id=$1::uuid and is_active",
        generated_entry.id);

    const auto summary = repository.load_summary(revision_id);
    ASSERT_TRUE(summary.has_value());
    const auto blockers = (*summary)["blockers"];

    // 待确认那批只进计数，不进样本——界面上它们由"N 个构件的规范映射待确认"
    // 那一行代表，进样本会被数两遍。
    EXPECT_EQ(blockers["by_code"]["component_mapping_required"].asInt(), 2);
    EXPECT_EQ(blockers["individual_total"].asInt(), 1);
    EXPECT_EQ(blockers["total"].asInt(), 2);
    ASSERT_EQ(blockers["samples"].size(), 1u);
    EXPECT_EQ(blockers["samples"][0]["entity_type"].asString(), "inventory_entry");
    EXPECT_EQ(blockers["samples"][0]["site_component_type"].asString(), "自定义类型");
    EXPECT_FALSE(blockers["samples"][0]["position"].isNull());
    EXPECT_EQ(blockers["by_code"]["inventory_empty"].asInt(), 0);
}

// ---------------------------------------------------------------------------
// 分组分页与编号搜索。
// ---------------------------------------------------------------------------

TEST_F(ComponentInventoryRepositoryTest, GroupEntriesPageKeepsPositionAcrossDeactivatedEntries) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = created.revision->id;

    std::vector<std::string> ids;
    for (int index = 0; index < 5; ++index) {
        db::InventoryNewEntry manual;
        manual.component_number = "B-" + std::to_string(index);
        manual.site_name = "支座";
        manual.site_component_type = "支座";
        manual.sort_order = 100 + index;
        const auto added = repository.add_entry(revision_id, user_id, manual);
        ASSERT_EQ(added.status, db::ComponentInventoryStatus::Ok);
        ASSERT_TRUE(added.entry.has_value());
        ids.push_back(added.entry->entry.id);
    }
    ASSERT_EQ(ids.size(), 5u);

    // 停用中间一条：它仍然占位，后面几条的序号不能因此前移，否则"定位"会跳错页。
    ASSERT_EQ(repository.deactivate_entry(revision_id, ids[2], user_id, "现场已拆除").status,
              db::ComponentInventoryStatus::Ok);

    const auto page = repository.load_group_entries(revision_id, "支座", 0, 100);
    EXPECT_EQ(page.total, 5) << "total 含停用构件";
    ASSERT_EQ(page.entries.size(), 5u);
    for (std::size_t index = 0; index < page.entries.size(); ++index) {
        EXPECT_EQ(page.entries[index].position, static_cast<std::int64_t>(index));
        EXPECT_EQ(page.entries[index].entry.component_number, "B-" + std::to_string(index));
    }
    EXPECT_FALSE(page.entries[2].entry.is_active);

    // 分页与越界：越界页返回空列表但 total 仍是真实值，前端据此夹取页码。
    const auto second = repository.load_group_entries(revision_id, "支座", 2, 2);
    ASSERT_EQ(second.entries.size(), 2u);
    EXPECT_EQ(second.entries.front().position, 2);
    const auto beyond = repository.load_group_entries(revision_id, "支座", 500, 100);
    EXPECT_TRUE(beyond.entries.empty());
    EXPECT_EQ(beyond.total, 5) << "越界页也要带回真实 total";
}

TEST_F(ComponentInventoryRepositoryTest, GroupEntriesReturnOnlyActiveMappings) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = created.revision->id;
    const auto entry = created.revision->entries.front();

    // 造一条失效的历史映射。全量装配不过滤 is_active，会把历次改动积累的失效映射
    // 一并带出，单页体积随之不可控；而前端所有消费点都只读生效映射。
    client->execSqlSync(
        "insert into bridge_component_standard_mappings"
        "(inventory_entry_id,standard_package_id,standard_bridge_type_id,"
        "standard_component_category_id,structure_part,mapping_source,confirmation_status,"
        "is_active) values($1::uuid,$2::uuid,$3,$4,'superstructure','历史','待确认',false)",
        entry.id, package_id, input.bridge_type_id, "test.component.main_girder");

    const auto page = repository.load_group_entries(revision_id, entry.site_component_type, 0, 100);
    ASSERT_EQ(page.entries.size(), 1u);
    ASSERT_FALSE(page.entries.front().entry.mappings.empty());
    for (const auto& mapping : page.entries.front().entry.mappings) {
        EXPECT_TRUE(mapping.is_active) << "失效映射不该出现在明细里";
    }
}

TEST_F(ComponentInventoryRepositoryTest, SearchMatchesSubstringAndEscapesWildcards) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = created.revision->id;

    int order = 200;
    for (const auto* number : {"3-5#梁", "13-5#梁", "23-5#梁", "100%特殊"}) {
        db::InventoryNewEntry manual;
        manual.component_number = number;
        manual.site_name = "梁";
        manual.site_component_type = "检索用类型";
        manual.sort_order = order;
        order += 1;
        ASSERT_EQ(repository.add_entry(revision_id, user_id, manual).status,
                  db::ComponentInventoryStatus::Ok);
    }

    // 子串语义与前端原来的 String.includes 一致：搜 3-5 也会命中 13-5#梁 和 23-5#梁。
    const auto hits = repository.search_entries(revision_id, "3-5", 50);
    EXPECT_EQ(hits.total, 3);
    ASSERT_EQ(hits.entries.size(), 3u);

    // 截断时 total 仍是未截断的命中数——界面上"匹配 N 个构件，显示前 M 个"依赖它。
    const auto truncated = repository.search_entries(revision_id, "3-5", 2);
    EXPECT_EQ(truncated.total, 3);
    EXPECT_EQ(truncated.entries.size(), 2u);

    // 通配符必须按字面匹配，否则搜一个 % 就命中全表。
    const auto wildcard = repository.search_entries(revision_id, "%", 50);
    EXPECT_EQ(wildcard.total, 1);
    ASSERT_EQ(wildcard.entries.size(), 1u);
    EXPECT_EQ(wildcard.entries.front().entry.component_number, "100%特殊");

    // 零命中：窗口函数在没有行时带不回总数，实现必须显式规定 total = 0。
    const auto empty = repository.search_entries(revision_id, "不存在的编号", 50);
    EXPECT_EQ(empty.total, 0);
    EXPECT_TRUE(empty.entries.empty());
}

TEST_F(ComponentInventoryRepositoryTest, PositionAgreesBetweenPageSearchAndBlockerSample) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = created.revision->id;

    // 前两条有映射、第三条没有：第三条会同时出现在分页、搜索和 blocker 样本里，
    // 三处的组内序号必须一致，否则"定位"按钮算出来的页码是错的。
    std::string unmapped_id;
    int order = 300;
    for (const auto* number : {"P-1", "P-2", "P-3"}) {
        db::InventoryNewEntry manual;
        manual.component_number = number;
        manual.site_name = "定位用构件";
        manual.site_component_type = "定位用类型";
        manual.sort_order = order;
        order += 1;
        const auto added = repository.add_entry(revision_id, user_id, manual);
        ASSERT_EQ(added.status, db::ComponentInventoryStatus::Ok);
        ASSERT_TRUE(added.entry.has_value());
        const auto& entry = added.entry->entry;
        if (std::string(number) == "P-3") {
            unmapped_id = entry.id;
        } else {
            db::InventoryMappingUpdate mapping;
            mapping.standard_package_id = package_id;
            mapping.standard_bridge_type_id = input.bridge_type_id;
            mapping.standard_component_category_id = "test.component.main_girder";
            mapping.structure_part = "superstructure";
            ASSERT_EQ(repository.set_mapping(revision_id, entry.id, user_id, mapping).status,
                      db::ComponentInventoryStatus::Ok);
        }
    }
    ASSERT_FALSE(unmapped_id.empty());

    std::int64_t page_position = -1;
    for (const auto& located : repository.load_group_entries(revision_id, "定位用类型", 0, 100).entries) {
        if (located.entry.id == unmapped_id) page_position = located.position;
    }
    ASSERT_NE(page_position, -1);
    EXPECT_EQ(page_position, 2);

    std::int64_t search_position = -1;
    for (const auto& located : repository.search_entries(revision_id, "P-3", 50).entries) {
        if (located.entry.id == unmapped_id) search_position = located.position;
    }
    EXPECT_EQ(search_position, page_position) << "搜索结果与分页必须用同一套序号";

    const auto summary = repository.load_summary(revision_id);
    ASSERT_TRUE(summary.has_value());
    std::int64_t sample_position = -1;
    for (const auto& sample : (*summary)["blockers"]["samples"]) {
        if (sample["entity_id"].asString() == unmapped_id) {
            sample_position = sample["position"].asInt64();
        }
    }
    EXPECT_EQ(sample_position, page_position) << "blocker 样本必须用同一套序号";
}

// 写响应的形状。汇总在提交前的同一个事务里算出，所以它反映的必须是这次写入之后的
// 状态；派生草稿时还必须基于派生出来的那个修订版，而不是请求里带的那个。
TEST_F(ComponentInventoryRepositoryTest, WriteSummaryFollowsDerivedDraftAndCarriesChangedEntry) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    ASSERT_TRUE(created.summary.has_value()) << "生成台账也要带回汇总";
    const auto entry = created.revision->entries.front();
    const auto confirmed = repository.confirm_revision(created.revision->id, user_id, "基线版");
    ASSERT_EQ(confirmed.status, db::ComponentInventoryStatus::Ok);
    const auto confirmed_id = revision_id_of(confirmed);

    db::InventoryEntryUpdate update;
    update.component_number = "派生后编号";
    update.site_name = entry.site_name;
    update.site_component_type = entry.site_component_type;
    update.span_or_location = entry.span_or_location;
    const auto derived = repository.update_entry(confirmed_id, entry.id, user_id, update);
    ASSERT_EQ(derived.status, db::ComponentInventoryStatus::Ok);
    ASSERT_TRUE(derived.summary.has_value());

    const auto derived_id = revision_id_of(derived);
    EXPECT_NE(derived_id, confirmed_id) << "写已确认版本应派生新草稿";
    EXPECT_EQ((*derived.summary)["revision"]["status"].asString(), "草稿")
        << "汇总必须基于派生出来的草稿，而不是请求里那个已确认版本";

    // 受影响构件随响应带回，前端据此就地补页，不必再拉一遍。
    ASSERT_TRUE(derived.entry.has_value());
    EXPECT_EQ(derived.entry->entry.component_number, "派生后编号");
    EXPECT_EQ(derived.entry->entry.bridge_component_id, entry.bridge_component_id)
        << "派生后仍是同一个实际构件";
    EXPECT_EQ(derived.entry->position, 0);

    // 汇总确实取自新草稿：拿它的 id 单独再查一次，编号应当已经是改后的。
    const auto reread = repository.load_summary(derived_id);
    ASSERT_TRUE(reread.has_value());
    ASSERT_EQ((*reread)["groups"].size(), 1u);
    EXPECT_EQ((*reread)["groups"][0]["first_number"].asString(), "派生后编号");
}

TEST_F(ComponentInventoryRepositoryTest, WritesWithoutASingleTargetOmitTheChangedEntry) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 2);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = created.revision->id;

    db::InventoryNewEntry manual;
    manual.component_number = "待删除-1";
    manual.site_name = "临时构件";
    manual.site_component_type = "临时类型";
    const auto added = repository.add_entry(revision_id, user_id, manual);
    ASSERT_EQ(added.status, db::ComponentInventoryStatus::Ok);
    ASSERT_TRUE(added.entry.has_value()) << "新增有明确的受影响构件";

    // 删除之后那条构件已经不存在，回传它没有意义。
    const auto removed = repository.delete_entry(revision_id, added.entry->entry.id, user_id);
    ASSERT_EQ(removed.status, db::ComponentInventoryStatus::Ok);
    ASSERT_TRUE(removed.summary.has_value());
    EXPECT_FALSE(removed.entry.has_value());
    for (const auto& group : (*removed.summary)["groups"]) {
        EXPECT_NE(group["site_component_type"].asString(), "临时类型")
            << "删掉最后一条后该组不该还在汇总里";
    }

    // 批量确认映射天然影响所有分组，同样没有单一的受影响构件。
    const auto bulk = repository.confirm_pending_mappings(revision_id, user_id, "");
    ASSERT_EQ(bulk.status, db::ComponentInventoryStatus::Ok);
    ASSERT_TRUE(bulk.summary.has_value());
    EXPECT_FALSE(bulk.entry.has_value());
}

// 搬迁验收的导出口。compare-inventory-summary.ps1 需要"新实现"那一侧的输出，
// 而汇总 SQL 只存在于 load_summary() 里。与其在脚本里复制一份 SQL——那正是这次
// 要消灭的分叉——不如让脚本调真正的实现。
//
// 两个环境变量都不设时跳过，所以它在常规测试运行里是惰性的。
TEST(ComponentInventorySummaryDumpTest, DumpsSummaryForParityScript) {
    const char* revision = std::getenv("INVENTORY_PARITY_REVISION");
    const char* destination = std::getenv("INVENTORY_PARITY_OUT");
    if (revision == nullptr || destination == nullptr) GTEST_SKIP();

    auto client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);
    db::ComponentInventoryRepository repository(client);
    const auto summary = repository.load_summary(revision);
    ASSERT_TRUE(summary.has_value()) << "修订版 " << revision << " 不存在";

    std::ofstream file(destination, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << "无法写入 " << destination;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    file << Json::writeString(builder, *summary);
    file.close();
    client->closeAll();
}
