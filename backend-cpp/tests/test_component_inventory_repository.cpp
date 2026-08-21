#include <chrono>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include <algorithm>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/inventory/ComponentInventoryGenerator.hpp"
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

// 写方法不再回传全量修订版（HTTP 响应里也不发）；修订版 id 从汇总里取。
std::string revision_id_of(const db::ComponentInventoryOutcome& outcome) {
    return (*outcome.summary)["revision"]["id"].asString();
}

// 需要逐条构件的用例显式取一次全量。get_revision() 仍然保留——/latest 和校对
// 工作台的构件选择器都在用它。
inventory::InventoryRevision entries_of(
    const db::ComponentInventoryRepository& repository,
    const db::ComponentInventoryOutcome& outcome) {
    return *repository.get_revision(revision_id_of(outcome));
}

}  // namespace

// 病害匹配走的是精简装配：只取草稿引用到的构件，且不算 is_referenced（那是每条构件
// 四个 exists 子查询，5174 条的桥上占了整份装配的大头，而匹配从不读它）。
//
// 快没有意义，如果它顺带改了结论。这条逐字段核对：点名的那些构件，精简版必须与
// 完整版给出同样的条目和同样的映射（顺序也一样，匹配是按顺序取第一个命中的映射）。
TEST_F(ComponentInventoryRepositoryTest, NarrowLoadMatchesTheFullLoadForTheRequestedComponents) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(5, 6);
    const auto generated = inventory::generate_component_inventory(input);
    ASSERT_TRUE(generated.ok());

    db::ComponentInventoryRepository repository(client);
    const auto created = repository.generate_draft(
        bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    ASSERT_EQ(
        repository.confirm_revision(revision_id_of(created), user_id, "供精简装配比对")
            .status,
        db::ComponentInventoryStatus::Ok);

    const auto full = repository.resolve_confirmed_revision(bridge_id, std::nullopt);
    ASSERT_TRUE(full.has_value());
    ASSERT_GT(full->entries.size(), 3u) << "样本太小，比不出什么";

    // 只点名其中三条，顺带混进一个不存在的 id——匹配时草稿里完全可能引用到已被
    // 移出台账的构件，那种情况应当是"这条查不到"，而不是整批出错。
    std::vector<std::string> wanted{
        full->entries[0].bridge_component_id,
        full->entries[2].bridge_component_id,
        full->entries[3].bridge_component_id,
        "00000000-0000-0000-0000-0000000000ff",
    };
    const auto narrow = repository.resolve_confirmed_revision_for_components(
        bridge_id, std::nullopt, wanted);
    ASSERT_TRUE(narrow.has_value());

    EXPECT_EQ(narrow->id, full->id) << "解析到的版本必须是同一个";
    EXPECT_EQ(narrow->status, full->status);
    ASSERT_EQ(narrow->entries.size(), 3u) << "不存在的 id 不该凭空造出条目";

    for (const auto& entry : narrow->entries) {
        const auto expected = std::find_if(
            full->entries.begin(), full->entries.end(),
            [&](const auto& item) {
                return item.bridge_component_id == entry.bridge_component_id;
            });
        ASSERT_NE(expected, full->entries.end());
        EXPECT_EQ(entry.id, expected->id);
        EXPECT_EQ(entry.component_number, expected->component_number);
        EXPECT_EQ(entry.site_component_type, expected->site_component_type);
        EXPECT_EQ(entry.is_active, expected->is_active);
        // 映射是匹配唯一真正要用的东西：数量、顺序、内容都必须一致。
        ASSERT_EQ(entry.mappings.size(), expected->mappings.size());
        for (std::size_t i = 0; i < entry.mappings.size(); ++i) {
            EXPECT_EQ(entry.mappings[i].id, expected->mappings[i].id);
            EXPECT_EQ(entry.mappings[i].standard_package_id,
                      expected->mappings[i].standard_package_id);
            EXPECT_EQ(entry.mappings[i].standard_bridge_type_id,
                      expected->mappings[i].standard_bridge_type_id);
            EXPECT_EQ(entry.mappings[i].standard_component_category_id,
                      expected->mappings[i].standard_component_category_id);
            EXPECT_EQ(entry.mappings[i].confirmation_status,
                      expected->mappings[i].confirmation_status);
            EXPECT_EQ(entry.mappings[i].is_active, expected->mappings[i].is_active);
        }
    }
}

// 病害校对列表按部件走查：上部结构 → 下部结构 → 桥面系，每段内部按台账目录里的
// 部件次序。梁式桥的样本台账只有梁与支座两类，两者同属上部结构，靠目录位置分先后
// （梁在目录第 18 位、支座第 45 位）。
TEST_F(ComponentInventoryRepositoryTest, ReviewOrderFollowsThePartCatalogNotTheInventorySortOrder) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(5, 6);
    const auto generated = inventory::generate_component_inventory(input);
    ASSERT_TRUE(generated.ok());
    db::ComponentInventoryRepository repository(client);
    const auto created = repository.generate_draft(
        bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    ASSERT_EQ(
        repository.confirm_revision(revision_id_of(created), user_id, "走查顺序").status,
        db::ComponentInventoryStatus::Ok);

    const auto full = repository.resolve_confirmed_revision(bridge_id, std::nullopt);
    ASSERT_TRUE(full.has_value());
    std::vector<std::string> ids;
    for (const auto& entry : full->entries) ids.push_back(entry.bridge_component_id);
    // 打乱输入，确认顺序来自排序规则而不是入参次序。
    std::reverse(ids.begin(), ids.end());

    const auto ordered = repository.order_components_for_review(
        bridge_id, std::nullopt, ids);
    ASSERT_EQ(ordered.size(), ids.size());

    std::map<std::string, const inventory::InventoryEntry*> by_component;
    for (const auto& entry : full->entries) by_component[entry.bridge_component_id] = &entry;

    // 同一部件的构件必须连成一段，不能被别的部件打断——这正是"逐部件核对"要的。
    std::vector<std::string> part_sequence;
    for (const auto& item : ordered) {
        // 返回里带的部件名必须与台账一致，前端要拿它做筛选下拉。
        ASSERT_EQ(item.part_name, by_component[item.bridge_component_id]->site_component_type);
        if (part_sequence.empty() || part_sequence.back() != item.part_name) {
            part_sequence.push_back(item.part_name);
        }
    }
    std::set<std::string> seen;
    for (const auto& type : part_sequence) {
        EXPECT_TRUE(seen.insert(type).second) << "部件 " << type << " 被切成了不连续的两段";
    }
    // 同一部件内部按台账生成次序，不能乱。
    int previous_sort = -1;
    std::string previous_type;
    for (const auto& item : ordered) {
        const auto* entry = by_component[item.bridge_component_id];
        if (entry->site_component_type != previous_type) {
            previous_type = entry->site_component_type;
            previous_sort = -1;
        }
        EXPECT_GT(entry->sort_order, previous_sort) << "同部件内部次序乱了";
        previous_sort = entry->sort_order;
    }
}

// 草稿里一条绑定都没有时不该白跑一趟数据库取条目，但版本本身仍要解析出来。
TEST_F(ComponentInventoryRepositoryTest, NarrowLoadWithNoComponentsStillResolvesTheRevision) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(5, 6);
    const auto generated = inventory::generate_component_inventory(input);
    ASSERT_TRUE(generated.ok());
    db::ComponentInventoryRepository repository(client);
    const auto created = repository.generate_draft(
        bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    ASSERT_EQ(
        repository.confirm_revision(revision_id_of(created), user_id, "空清单")
            .status,
        db::ComponentInventoryStatus::Ok);

    const auto narrow =
        repository.resolve_confirmed_revision_for_components(bridge_id, std::nullopt, {});
    ASSERT_TRUE(narrow.has_value());
    EXPECT_EQ(narrow->id, revision_id_of(created));
    EXPECT_TRUE(narrow->entries.empty());
}

TEST_F(ComponentInventoryRepositoryTest, GeneratedComponentsKeepStableIdentityWhenNumberChanges) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(5, 6);
    const auto generated = inventory::generate_component_inventory(input);
    ASSERT_TRUE(generated.ok());

    db::ComponentInventoryRepository repository(client);
    const auto created = repository.generate_draft(
        bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    ASSERT_TRUE(created.summary.has_value());
    ASSERT_EQ(entries_of(repository, created).entries.size(), 30u);

    std::set<std::string> physical_ids;
    for (const auto& entry : entries_of(repository, created).entries) {
        physical_ids.insert(entry.bridge_component_id);
    }
    EXPECT_EQ(physical_ids.size(), 30u);

    const auto original = entries_of(repository, created).entries.front();
    db::InventoryEntryUpdate update;
    update.component_number = "自定义-01";
    update.site_name = original.site_name;
    update.site_component_type = original.site_component_type;
    update.span_or_location = original.span_or_location;
    const auto updated = repository.update_entry(
        revision_id_of(created), original.id, user_id, update);
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
    const auto revision_one = revision_id_of(created);
    const auto entry_one = entries_of(repository, created).entries.front();

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
    ASSERT_EQ(entries_of(repository, created).entries.size(), 6u);
    for (const auto& entry : entries_of(repository, created).entries) {
        ASSERT_EQ(entry.mappings.size(), 1u);
        EXPECT_EQ(entry.mappings[0].confirmation_status, "已确认");
        EXPECT_EQ(entry.mappings[0].mapping_source, "模板生成");
    }
    // 生成后的台账不再有映射阻塞，可直接确认。
    EXPECT_EQ(
        repository.confirm_revision(revision_id_of(created), user_id, "生成即确认").status,
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
    const auto revision_id = revision_id_of(created);

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
    auto added = repository.add_entry(revision_id_of(created), user_id, manual);
    ASSERT_EQ(added.status, db::ComponentInventoryStatus::Ok);
    const auto confirmation = repository.confirm_revision(
        revision_id_of(created), user_id, "应被阻断");
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
    const auto entry = entries_of(repository, created).entries.front();

    db::InventoryMappingUpdate mapping;
    mapping.standard_package_id = package_id;
    mapping.standard_bridge_type_id = input.bridge_type_id;
    mapping.standard_component_category_id = "test.component.main_girder";
    mapping.structure_part = "superstructure";
    ASSERT_EQ(repository.set_mapping(revision_id_of(created), entry.id, user_id, mapping).status,
              db::ComponentInventoryStatus::Ok);

    mapping.standard_package_id = other_package_id;
    mapping.standard_component_category_id = "other-standard.component.main_girder";
    ASSERT_EQ(repository.set_mapping(revision_id_of(created), entry.id, user_id, mapping).status,
              db::ComponentInventoryStatus::Ok);

    // 把第二个包的映射改回待确认，构件于是同时挂着 [已确认(A)、待确认(B)]。
    client->execSqlSync(
        "update bridge_component_standard_mappings set confirmation_status='待确认',"
        "confirmed_by_user_id=null,confirmed_at=null "
        "where inventory_entry_id=$1::uuid and standard_package_id=$2::uuid and is_active",
        entry.id, other_package_id);

    const auto confirmation = repository.confirm_revision(
        revision_id_of(created), user_id, "任一已确认即可");
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
        const auto added = repository.add_entry(revision_id_of(created), user_id, manual);
        ASSERT_EQ(added.status, db::ComponentInventoryStatus::Ok);
        ASSERT_TRUE(added.entry.has_value());
        expected.insert(added.entry->entry.id);
    }
    ASSERT_EQ(expected.size(), 2u);

    const auto confirmation = repository.confirm_revision(revision_id_of(created), user_id, "应被阻断");
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
        revision_id_of(created));
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
    const auto first_entry = entries_of(repository, created).entries.front();

    db::InventoryMappingUpdate mapping;
    mapping.standard_package_id = package_id;
    mapping.standard_bridge_type_id = input.bridge_type_id;
    mapping.standard_component_category_id = "test.component.main_girder";
    mapping.structure_part = "superstructure";
    ASSERT_EQ(repository.set_mapping(revision_id_of(created), first_entry.id, user_id, mapping).status,
              db::ComponentInventoryStatus::Ok);
    const auto revision_one = repository.confirm_revision(revision_id_of(created), user_id, "第一版");
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
    const auto entries = entries_of(repository, created).entries;
    ASSERT_EQ(entries.size(), 2u);

    db::InventoryMappingUpdate mapping;
    mapping.standard_package_id = package_id;
    mapping.standard_bridge_type_id = input.bridge_type_id;
    mapping.standard_component_category_id = "test.component.main_girder";
    mapping.structure_part = "superstructure";
    for (const auto& entry : entries) {
        ASSERT_EQ(repository.set_mapping(revision_id_of(created), entry.id, user_id, mapping).status,
                  db::ComponentInventoryStatus::Ok);
    }
    const auto confirmed = repository.confirm_revision(revision_id_of(created), user_id, "基线版");
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

// 校对页要按 (桥型, 规范类别) 去查评定树上适用的病害节点。类别单独给不够用，
// 少了桥型，调用方就只能下载整份台账、从任意一条映射里把桥型翻出来。
TEST_F(ComponentInventoryRepositoryTest, SummaryGroupCarriesTheFullMappingScope) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = revision_id_of(created);

    const auto summary = repository.load_summary(revision_id);
    ASSERT_TRUE(summary.has_value());
    bool checked = false;
    for (const auto& group : (*summary)["groups"]) {
        if (group["standard_component_category_id"].isNull()) continue;
        EXPECT_FALSE(group["standard_bridge_type_id"].isNull())
            << "有类别就必然有桥型：两者出自同一条生效映射";
        EXPECT_EQ(group["standard_bridge_type_id"].asString(), input.bridge_type_id);
        checked = true;
    }
    EXPECT_TRUE(checked) << "夹具应当至少生成一个带映射的分组";
}

TEST_F(ComponentInventoryRepositoryTest, SummaryNumberRangeUsesTraversalOrderNotLexicographic) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = revision_id_of(created);

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
    const auto revision_id = revision_id_of(created);

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
    const auto revision_id = revision_id_of(created);
    const auto entry = entries_of(repository, created).entries.front();

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
    const auto revision_id = revision_id_of(created);
    const auto generated_entry = entries_of(repository, created).entries.front();

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
    const auto revision_id = revision_id_of(created);

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
    const auto revision_id = revision_id_of(created);
    const auto entry = entries_of(repository, created).entries.front();

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
    const auto revision_id = revision_id_of(created);

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
    const auto hits = repository.search_entries(revision_id, "3-5", false, 50);
    EXPECT_EQ(hits.total, 3);
    ASSERT_EQ(hits.entries.size(), 3u);

    // 截断时 total 仍是未截断的命中数——界面上"匹配 N 个构件，显示前 M 个"依赖它。
    const auto truncated = repository.search_entries(revision_id, "3-5", false, 2);
    EXPECT_EQ(truncated.total, 3);
    EXPECT_EQ(truncated.entries.size(), 2u);

    // 通配符必须按字面匹配，否则搜一个 % 就命中全表。
    const auto wildcard = repository.search_entries(revision_id, "%", false, 50);
    EXPECT_EQ(wildcard.total, 1);
    ASSERT_EQ(wildcard.entries.size(), 1u);
    EXPECT_EQ(wildcard.entries.front().entry.component_number, "100%特殊");

    // 零命中：窗口函数在没有行时带不回总数，实现必须显式规定 total = 0。
    const auto empty = repository.search_entries(revision_id, "不存在的编号", false, 50);
    EXPECT_EQ(empty.total, 0);
    EXPECT_TRUE(empty.entries.empty());
}

// 绑定面板按编号、构件类别、现场名称三个字段搜；台账页文案也据此改过。
TEST_F(ComponentInventoryRepositoryTest, SearchMatchesTypeAndSiteNameToo) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = revision_id_of(created);

    db::InventoryNewEntry manual;
    manual.component_number = "Z-1";
    manual.site_name = "东侧栏杆";
    manual.site_component_type = "人行道栏杆";
    manual.sort_order = 400;
    ASSERT_EQ(repository.add_entry(revision_id, user_id, manual).status,
              db::ComponentInventoryStatus::Ok);

    EXPECT_EQ(repository.search_entries(revision_id, "Z-1", false, 50).total, 1);
    EXPECT_EQ(repository.search_entries(revision_id, "人行道栏杆", false, 50).total, 1);
    EXPECT_EQ(repository.search_entries(revision_id, "东侧", false, 50).total, 1);
}

// binding_eligible 的两个条件缺一不可，而且必须在服务端、在 limit 之前生效。
TEST_F(ComponentInventoryRepositoryTest, BindingEligibleFiltersBeforeTheLimit) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = revision_id_of(created);

    // 前 20 条排在前面且都不可绑（停用），第 21 条才是唯一可绑的那条。
    // 先取 20 条再由调用方过滤的话，这 20 条会把它整个挤出结果。
    std::string usable_id;
    for (int i = 0; i < 21; ++i) {
        db::InventoryNewEntry manual;
        manual.component_number = "F-" + std::to_string(i);
        manual.site_name = "过滤用构件";
        manual.site_component_type = "过滤用类型";
        manual.sort_order = 500 + i;
        const auto added = repository.add_entry(revision_id, user_id, manual);
        ASSERT_EQ(added.status, db::ComponentInventoryStatus::Ok);
        const auto& entry = added.entry->entry;
        db::InventoryMappingUpdate mapping;
        mapping.standard_package_id = package_id;
        mapping.standard_bridge_type_id = input.bridge_type_id;
        mapping.standard_component_category_id = "test.component.main_girder";
        mapping.structure_part = "superstructure";
        ASSERT_EQ(repository.set_mapping(revision_id, entry.id, user_id, mapping).status,
                  db::ComponentInventoryStatus::Ok);
        if (i < 20) {
            ASSERT_EQ(repository.deactivate_entry(revision_id, entry.id, user_id, "测试停用").status,
                      db::ComponentInventoryStatus::Ok);
        } else {
            usable_id = entry.id;
        }
    }

    const auto all = repository.search_entries(revision_id, "F-", false, 20);
    EXPECT_EQ(all.total, 21) << "不过滤时应当看得见全部，含停用构件";

    const auto eligible = repository.search_entries(revision_id, "F-", true, 20);
    EXPECT_EQ(eligible.total, 1) << "total 必须与 entries 用同一套过滤范围";
    ASSERT_EQ(eligible.entries.size(), 1u);
    EXPECT_EQ(eligible.entries.front().entry.id, usable_id);
}

// 启用但没有生效映射的构件同样不可绑。只筛 is_active 会把它放进来，用户点了应用之后
// validate_target() 找不到映射，整批冲突。
TEST_F(ComponentInventoryRepositoryTest, BindingEligibleAlsoRequiresAnActiveMapping) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = revision_id_of(created);

    db::InventoryNewEntry manual;
    manual.component_number = "N-1";
    manual.site_name = "无映射构件";
    manual.site_component_type = "无映射类型";
    manual.sort_order = 600;
    const auto added = repository.add_entry(revision_id, user_id, manual);
    ASSERT_EQ(added.status, db::ComponentInventoryStatus::Ok);

    EXPECT_EQ(repository.search_entries(revision_id, "N-1", false, 50).total, 1);
    EXPECT_EQ(repository.search_entries(revision_id, "N-1", true, 50).total, 0);

    // 映射装配那条语句用的是同一段谓词。谓词在那里拼错时它返回零行，构件会整批
    // 丢掉映射而不报错——所以这里必须断言映射确实被带回来了。
    db::InventoryMappingUpdate mapping;
    mapping.standard_package_id = package_id;
    mapping.standard_bridge_type_id = input.bridge_type_id;
    mapping.standard_component_category_id = "test.component.main_girder";
    mapping.structure_part = "superstructure";
    ASSERT_EQ(repository.set_mapping(revision_id, added.entry->entry.id, user_id, mapping).status,
              db::ComponentInventoryStatus::Ok);

    const auto hits = repository.search_entries(revision_id, "N-1", true, 50);
    ASSERT_EQ(hits.entries.size(), 1u);
    EXPECT_FALSE(hits.entries.front().entry.mappings.empty()) << "映射装配丢了整批映射";
}

TEST_F(ComponentInventoryRepositoryTest, PositionAgreesBetweenPageSearchAndBlockerSample) {
    if (!client) GTEST_SKIP();
    const auto input = girder_input(1, 1);
    const auto generated = inventory::generate_component_inventory(input);
    db::ComponentInventoryRepository repository(client);
    auto created = repository.generate_draft(bridge_id, user_id, input, generated.entries);
    ASSERT_EQ(created.status, db::ComponentInventoryStatus::Ok);
    const auto revision_id = revision_id_of(created);

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
    for (const auto& located : repository.search_entries(revision_id, "P-3", false, 50).entries) {
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
    const auto entry = entries_of(repository, created).entries.front();
    const auto confirmed = repository.confirm_revision(revision_id_of(created), user_id, "基线版");
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
    const auto revision_id = revision_id_of(created);

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

// ---------------------------------------------------------------------------
// 版本解析规则（六处共用）：检测年度锁定的版本优先，年度未锁定时取该桥最新的
// 已确认版本。绑定写病害、保存校对草稿、入库前检查、年度确认、评定树自动匹配和
// Word 导入全部走这一条；各自实现一份的话规则迟早漂移。
// ---------------------------------------------------------------------------
class ConfirmedRevisionResolutionTest : public ComponentInventoryRepositoryTest {
protected:
    // 直接建版本行，不走生成/确认流程：这里要验的是解析规则本身，
    // 夹具越薄越不容易把别的业务规则牵扯进来。
    std::string add_revision(int revision_number, bool confirmed,
                             const std::string& owner_bridge_id = std::string()) const {
        const auto& target_bridge = owner_bridge_id.empty() ? bridge_id : owner_bridge_id;
        const auto id = client->execSqlSync(
            "insert into bridge_component_inventory_revisions"
            "(bridge_id,revision_number,created_by_user_id) values($1::uuid,$2,$3::uuid) "
            "returning id::text as id",
            target_bridge, revision_number, user_id)[0]["id"].as<std::string>();
        if (confirmed) {
            client->execSqlSync(
                "update bridge_component_inventory_revisions set status='已确认',"
                "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
                id, user_id);
        }
        return id;
    }

    std::string add_pending_year(int inspection_year = 2026) const {
        return client->execSqlSync(
            "insert into inspection_years(bridge_id,inspection_year,status,version_number,is_current) "
            "values($1::uuid,$2,'待校对',1,false) returning id::text as id",
            bridge_id, inspection_year)[0]["id"].as<std::string>();
    }

    std::optional<std::string> year_revision(const std::string& year_id) const {
        const auto rows = client->execSqlSync(
            "select component_inventory_revision_id::text as id from inspection_years "
            "where id=$1::uuid",
            year_id);
        if (rows.empty() || rows[0]["id"].isNull()) return std::nullopt;
        return rows[0]["id"].as<std::string>();
    }
};

TEST_F(ConfirmedRevisionResolutionTest, PrefersTheYearLockedRevisionOverNewerOnes) {
    if (!client) GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
    const auto r1 = add_revision(1, /*confirmed=*/true);
    add_revision(2, /*confirmed=*/true);
    add_revision(3, /*confirmed=*/false);
    db::ComponentInventoryRepository repository(client);

    // 年度锁着 R1，桥上另有更新的已确认 R2 与草稿 R3 -> 必须仍用 R1。
    // 历史年度的病害描述的就是 R1 那份台账，跟着最新版本走等于篡改历史。
    const auto ref = repository.resolve_confirmed_revision_ref(bridge_id, r1);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->id, r1);
    EXPECT_EQ(ref->bridge_id, bridge_id);

    // 完整装配那条必须给出同一个版本：两者共用同一套规则。
    const auto full = repository.resolve_confirmed_revision(bridge_id, r1);
    ASSERT_TRUE(full.has_value());
    EXPECT_EQ(full->id, r1);
}

TEST_F(ConfirmedRevisionResolutionTest, FallsBackToTheLatestConfirmedWhenTheYearIsUnlocked) {
    if (!client) GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
    add_revision(1, /*confirmed=*/true);
    const auto r2 = add_revision(2, /*confirmed=*/true);
    add_revision(3, /*confirmed=*/false);
    db::ComponentInventoryRepository repository(client);

    // 年度未锁定：取编号最大的**已确认**版本 R2，而不是编号更大的草稿 R3。
    const auto ref = repository.resolve_confirmed_revision_ref(bridge_id, std::nullopt);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->id, r2);
}

TEST_F(ConfirmedRevisionResolutionTest, RejectsALockedRevisionThatIsStillADraft) {
    if (!client) GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
    add_revision(1, /*confirmed=*/true);
    const auto draft = add_revision(2, /*confirmed=*/false);
    db::ComponentInventoryRepository repository(client);

    // 待校对年度可以合法地锁在草稿上（011 的触发器只对已确认/已被修订/已归档年度
    // 要求已确认版本）。解析器不能因此退回"最新已确认"——那样年度锁定就形同虚设。
    EXPECT_FALSE(repository.resolve_confirmed_revision_ref(bridge_id, draft).has_value());
    EXPECT_FALSE(repository.resolve_confirmed_revision(bridge_id, draft).has_value());
}

TEST_F(ConfirmedRevisionResolutionTest, RejectsALockedRevisionFromAnotherBridge) {
    if (!client) GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
    add_revision(1, /*confirmed=*/true);
    const auto other_bridge = client->execSqlSync(
        "insert into bridges(bridge_name) values('构件台账解析测试的另一座桥') "
        "returning id::text as id")[0]["id"].as<std::string>();
    const auto foreign_revision = add_revision(1, /*confirmed=*/true, other_bridge);
    db::ComponentInventoryRepository repository(client);

    EXPECT_FALSE(
        repository.resolve_confirmed_revision_ref(bridge_id, foreign_revision).has_value());

    client->execSqlSync("delete from bridges where id=$1::uuid", other_bridge);
}

TEST_F(ConfirmedRevisionResolutionTest, RejectsALockedRevisionThatNoLongerExists) {
    if (!client) GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
    add_revision(1, /*confirmed=*/true);
    db::ComponentInventoryRepository repository(client);

    EXPECT_FALSE(repository
                     .resolve_confirmed_revision_ref(
                         bridge_id, std::string("00000000-0000-0000-0000-000000000000"))
                     .has_value());
}

TEST_F(ConfirmedRevisionResolutionTest, ReturnsNothingWhenTheBridgeHasNoConfirmedRevision) {
    if (!client) GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
    add_revision(1, /*confirmed=*/false);
    db::ComponentInventoryRepository repository(client);

    // "桥上没有已确认版本"与"取到草稿"归进同一分支（都返回空）之后，入库前检查与
    // 年度确认仍必须照旧阻塞——调用方判的是 has_value()，两者行为一致。
    EXPECT_FALSE(repository.resolve_confirmed_revision_ref(bridge_id, std::nullopt).has_value());
}

// ---------------------------------------------------------------------------
// 年度版本锁定：写操作专用。年度已锁定时只做一致性确认，绝不覆盖。
// ---------------------------------------------------------------------------

TEST_F(ConfirmedRevisionResolutionTest, LocksAPendingYearThatHasNoRevisionYet) {
    if (!client) GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
    const auto r1 = add_revision(1, /*confirmed=*/true);
    const auto year_id = add_pending_year();
    db::ComponentInventoryRepository repository(client);

    EXPECT_TRUE(repository.lock_pending_year_revision(year_id, bridge_id, std::nullopt, r1));
    EXPECT_EQ(year_revision(year_id).value_or(""), r1);
}

TEST_F(ConfirmedRevisionResolutionTest, ConfirmsWithoutOverwritingAnAlreadyLockedYear) {
    if (!client) GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
    const auto r1 = add_revision(1, /*confirmed=*/true);
    const auto r2 = add_revision(2, /*confirmed=*/true);
    const auto year_id = add_pending_year();
    client->execSqlSync(
        "update inspection_years set component_inventory_revision_id=$2::uuid where id=$1::uuid",
        year_id, r1);
    db::ComponentInventoryRepository repository(client);

    // 调用方已读到锁定版本 R1：同版本放行，不同版本拒绝，两种情况都不写年度。
    EXPECT_TRUE(repository.lock_pending_year_revision(year_id, bridge_id, r1, r1));
    EXPECT_FALSE(repository.lock_pending_year_revision(year_id, bridge_id, r1, r2));
    EXPECT_EQ(year_revision(year_id).value_or(""), r1);
}

TEST_F(ConfirmedRevisionResolutionTest, DetectsAConcurrentLockToADifferentRevision) {
    if (!client) GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
    const auto r1 = add_revision(1, /*confirmed=*/true);
    const auto r2 = add_revision(2, /*confirmed=*/true);
    const auto year_id = add_pending_year();
    db::ComponentInventoryRepository repository(client);

    // 并发场景：调用方读到年度未锁定（传 nullopt），但在它下手之前别人锁到了 R2。
    // 带 "component_inventory_revision_id is null" 谓词的 UPDATE 命中 0 行，
    // 回读发现锁的是别的版本 -> 必须返回 false，让调用方整体回滚。
    client->execSqlSync(
        "update inspection_years set component_inventory_revision_id=$2::uuid where id=$1::uuid",
        year_id, r2);

    EXPECT_FALSE(repository.lock_pending_year_revision(year_id, bridge_id, std::nullopt, r1));
    EXPECT_EQ(year_revision(year_id).value_or(""), r2) << "抢锁失败不得覆盖别人锁定的版本";
    // 抢到的正好是同一个版本时放行：这不是冲突，本次要写的就是它。
    EXPECT_TRUE(repository.lock_pending_year_revision(year_id, bridge_id, std::nullopt, r2));
}

TEST_F(ConfirmedRevisionResolutionTest, SucceedsWithoutAYearWhenTheRecordIsUnmounted) {
    if (!client) GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
    const auto r1 = add_revision(1, /*confirmed=*/true);
    db::ComponentInventoryRepository repository(client);

    // 导入记录没挂年度时无处可锁，不该因此失败。
    EXPECT_TRUE(repository.lock_pending_year_revision(std::nullopt, bridge_id, std::nullopt, r1));
}

// 台账管理页依赖草稿优先的排序才能看见自己刚派生的草稿；解析器改造不得波及它。
TEST_F(ConfirmedRevisionResolutionTest, ManagementLookupStillPrefersTheDraft) {
    if (!client) GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
    add_revision(1, /*confirmed=*/true);
    add_revision(2, /*confirmed=*/true);
    const auto draft = add_revision(3, /*confirmed=*/false);
    db::ComponentInventoryRepository repository(client);

    EXPECT_EQ(repository.find_latest_revision_id(bridge_id).value_or(""), draft);
}


// 大桥生成回归：5000+ 构件时，紧跟批量插入的那条汇总查询会因为统计信息还停在
// "接近空表"而选出灾难性计划——实测 18.6 秒，而 DbClient 单语句超时是 10 秒，
// 于是整个生成以 SQL execution timeout 失败，界面上是"构件台账写入失败"，重试永远无解。
//
// 这条按生产超时（10 秒/语句）跑真实规模；去掉 generate_draft 里的 ANALYZE 即转红。
TEST_F(ComponentInventoryRepositoryTest, GeneratesALargeBridgeWithinTheStatementTimeout) {
    if (!client) GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
    inventory::GenerateInventoryInput input;
    input.standard_package_id = package_id;
    input.bridge_type_id = "test.bridge.beam";
    input.span_count = 33;
    // 与线上那座 33 孔桥同量级：每孔 25 片板、每孔每墩 50 个支座是主要来源。
    input.part_selections.push_back({"beam.girder", "板", {25}, {}});
    input.part_selections.push_back({"bearing.support", "支座", {50}, {}});
    input.part_selections.push_back({"lower.pier_column", "墩柱", {4}, {}});
    input.part_selections.push_back({"lower.riverbed", "河床", {}, {}});

    const auto generated = inventory::generate_component_inventory(input);
    ASSERT_TRUE(generated.ok()) << generated.error_message;
    ASSERT_GE(generated.entries.size(), 4000u) << "样本必须大到能触发坏计划";

    db::ComponentInventoryRepository repository(client);
    const auto outcome = repository.generate_draft(bridge_id, user_id, input, generated.entries);

    EXPECT_EQ(outcome.status, db::ComponentInventoryStatus::Ok)
        << "大桥生成不得超时；失败时真实原因见服务端日志";
    ASSERT_TRUE(outcome.summary.has_value());
    EXPECT_EQ((*outcome.summary)["revision"]["active_entry_count"].asInt64(),
              static_cast<Json::Int64>(generated.entries.size()));
}
