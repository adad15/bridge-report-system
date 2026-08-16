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
    for (const auto& entry : partial.revision->entries) {
        const auto expected = entry.site_component_type == "横隔板" ? "已确认" : "待确认";
        ASSERT_EQ(entry.mappings.size(), 1u);
        EXPECT_EQ(entry.mappings[0].confirmation_status, expected);
    }

    // 不带筛选时确认全部剩余映射。
    auto all = repository.confirm_pending_mappings(revision_id, user_id, "");
    ASSERT_EQ(all.status, db::ComponentInventoryStatus::Ok);
    for (const auto& entry : all.revision->entries) {
        ASSERT_EQ(entry.mappings.size(), 1u);
        EXPECT_EQ(entry.mappings[0].confirmation_status, "已确认");
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
        for (const auto& entry : added.revision->entries) {
            if (entry.component_number == number) expected.insert(entry.id);
        }
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
    const auto revision_one_id = revision_one.revision->id;

    // 对已确认版本写入会派生草稿，确认它得到第二个已确认版本。
    db::InventoryEntryUpdate update;
    update.component_number = "派生-01";
    update.site_name = first_entry.site_name;
    update.site_component_type = first_entry.site_component_type;
    update.span_or_location = first_entry.span_or_location;
    auto derived = repository.update_entry(revision_one_id, first_entry.id, user_id, update);
    ASSERT_EQ(derived.status, db::ComponentInventoryStatus::Ok);
    ASSERT_NE(derived.revision->id, revision_one_id) << "写入已确认版本应派生出新草稿";
    const auto revision_two = repository.confirm_revision(derived.revision->id, user_id, "第二版");
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
    const auto baseline_id = confirmed.revision->id;

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
    EXPECT_EQ(second_write.revision->id, first_write.revision->id);

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
        for (const auto& entry : added.revision->entries) {
            if (entry.component_number == number) cone_ids.push_back(entry.id);
        }
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
