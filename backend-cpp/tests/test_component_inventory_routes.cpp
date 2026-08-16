#include <string>

#include <gtest/gtest.h>

#include "bridge_report/http/ComponentInventoryRoutes.hpp"

namespace http = bridge_report::http;
namespace inventory = bridge_report::inventory;

TEST(ComponentInventoryRoutesTest, EntryRequiresNumberNameAndSiteType) {
    Json::Value valid;
    valid["component_number"] = "1-1#";
    valid["site_name"] = "1-1#主梁";
    valid["site_component_type"] = "主梁";
    bridge_report::db::InventoryEntryUpdate output;
    std::string message;
    EXPECT_TRUE(http::parse_inventory_entry_update(valid, output, message));

    valid["component_number"] = "";
    EXPECT_FALSE(http::parse_inventory_entry_update(valid, output, message));
}

TEST(ComponentInventoryRoutesTest, ParsesPartSelections) {
    Json::Value body;
    body["standard_package_id"] = "11111111-1111-1111-1111-111111111111";
    body["bridge_type_id"] = "h21.bridge_type.beam";
    body["span_count"] = 5;  // 目录路径无需 template_id
    Json::Value selection;
    selection["part_key"] = "beam.girder";
    selection["site_name"] = "空心板";
    selection["counts"].append(13);
    body["part_selections"].append(selection);

    inventory::GenerateInventoryInput output;
    std::string code, message;
    ASSERT_TRUE(inventory::parse_generate_inventory_input(body, output, code, message)) << message;
    ASSERT_EQ(output.part_selections.size(), 1u);
    EXPECT_EQ(output.part_selections[0].part_key, "beam.girder");
    EXPECT_EQ(output.part_selections[0].site_name, "空心板");
    ASSERT_EQ(output.part_selections[0].counts.size(), 1u);
    EXPECT_EQ(output.part_selections[0].counts[0], 13);
}

TEST(ComponentInventoryRoutesTest, RejectsMissingPartSelections) {
    Json::Value body;
    body["standard_package_id"] = "11111111-1111-1111-1111-111111111111";
    body["bridge_type_id"] = "h21.bridge_type.beam";
    body["span_count"] = 5;
    inventory::GenerateInventoryInput output;
    std::string code, message;
    EXPECT_FALSE(inventory::parse_generate_inventory_input(body, output, code, message));
    EXPECT_EQ(code, "inventory_part_selections_required");
}

TEST(ComponentInventoryRoutesTest, ValidatesPartSelectionsAgainstCatalogAndTaxonomy) {
    bridge_report::standards::StandardPackage package;
    bridge_report::standards::StandardDefinition category;
    category.id = "h21.component.beam.upper_bearing";
    category.source_file = "component-taxonomy.json";
    category.payload["bridge_type_ids"].append("h21.bridge_type.beam");
    category.payload["structure_part"] = "superstructure";
    category.payload["generatable"] = true;
    package.definitions.emplace(category.id, category);

    inventory::GenerateInventoryInput input;
    input.bridge_type_id = "h21.bridge_type.beam";
    input.span_count = 5;
    input.part_selections.push_back({"beam.girder", "空心板", {13}});
    std::string code, message;
    EXPECT_TRUE(http::validate_inventory_generation_standard(input, package, code, message))
        << message;

    // 部件不在部件目录。
    input.part_selections = {{"beam.nope", "?", {1}}};
    EXPECT_FALSE(http::validate_inventory_generation_standard(input, package, code, message));
    EXPECT_EQ(code, "unknown_part_key");

    // 数量维个数与目录定义不符（梁片数缺失）。
    input.part_selections = {{"beam.girder", "空心板", {}}};
    EXPECT_FALSE(http::validate_inventory_generation_standard(input, package, code, message));
    EXPECT_EQ(code, "inventory_part_count_mismatch");

    // 规范包缺少该部件的规范类别，不能静默归类。
    input.part_selections = {{"beam.girder", "空心板", {13}}};
    bridge_report::standards::StandardPackage empty_package;
    EXPECT_FALSE(http::validate_inventory_generation_standard(input, empty_package, code, message));
    EXPECT_EQ(code, "inventory_component_category_not_supported");
}

TEST(ComponentInventoryRoutesTest, SerializesPartCatalogForBridgeType) {
    bridge_report::standards::StandardPackage package;
    bridge_report::standards::StandardDefinition girder_cat;
    girder_cat.id = "h21.component.beam.upper_bearing";
    girder_cat.source_file = "component-taxonomy.json";
    girder_cat.payload["bridge_type_ids"].append("h21.bridge_type.beam");
    girder_cat.payload["structure_part"] = "superstructure";
    girder_cat.payload["name"] = "上部承重构件";
    girder_cat.payload["generatable"] = true;
    package.definitions.emplace(girder_cat.id, girder_cat);

    const Json::Value out = http::serialize_part_catalog(package, "h21.bridge_type.beam");
    ASSERT_TRUE(out.isArray());

    bool has_girder = false;
    for (const auto& part : out) {
        EXPECT_NE(part["part_key"].asString().rfind("arch.", 0), 0u);  // 包里无拱类别 → 无拱部件
        if (part["part_key"].asString() == "beam.girder") {
            has_girder = true;
            EXPECT_EQ(part["default_name"].asString(), "梁");
            EXPECT_EQ(part["structure_part"].asString(), "superstructure");
            EXPECT_EQ(part["standard_component_category_id"].asString(),
                      "h21.component.beam.upper_bearing");
            EXPECT_EQ(part["standard_component_category_name"].asString(), "上部承重构件");
            EXPECT_EQ(part["number_template"].asString(), "{span}-{c1}#{name}");
            EXPECT_FALSE(part["provisional"].asBool());
            ASSERT_TRUE(part["count_inputs"].isArray());
            ASSERT_EQ(part["count_inputs"].size(), 1u);
            EXPECT_EQ(part["count_inputs"][0]["key"].asString(), "girders_per_span");
            EXPECT_EQ(part["count_inputs"][0]["label"].asString(), "每孔梁片数");
        }
    }
    EXPECT_TRUE(has_girder);
}

// 单条构件的序列化是从 inventory_revision_json() 里抽出来的，供分组分页、编号搜索
// 和写响应共用。这两条用例钉住抽取没有走样：整份修订版里的每一条，必须与单独序列化
// 同一条构件的结果逐字段相同——否则聚合接口和 /latest 会给出两种形状。
TEST(ComponentInventoryModelsTest, RevisionEntriesMatchStandaloneEntrySerialization) {
    inventory::InventoryMapping mapping;
    mapping.id = "mapping-1";
    mapping.standard_package_id = "package-1";
    mapping.standard_bridge_type_id = "h21.bridge_type.beam";
    mapping.standard_component_category_id = "h21.component.beam.upper_bearing";
    mapping.structure_part = "superstructure";
    mapping.mapping_source = "模板生成";
    mapping.confirmation_status = "已确认";

    inventory::InventoryEntry entry;
    entry.id = "entry-1";
    entry.bridge_component_id = "component-1";
    entry.component_number = "1-1#梁";
    entry.site_name = "梁";
    entry.site_component_type = "梁";
    entry.sort_order = 7;
    entry.mappings.push_back(mapping);

    inventory::InventoryEntry deactivated = entry;
    deactivated.id = "entry-2";
    deactivated.bridge_component_id = "component-2";
    deactivated.component_number = "1-2#梁";
    deactivated.is_active = false;
    deactivated.deactivated_at = "2026-08-16T00:00:00Z";
    deactivated.deactivation_reason = "现场已拆除";
    deactivated.span_or_location = "第 1 孔";
    deactivated.remarks = "备注";

    inventory::InventoryRevision revision;
    revision.id = "revision-1";
    revision.bridge_id = "bridge-1";
    revision.revision_number = 2;
    revision.status = "草稿";
    revision.entries = {entry, deactivated};

    const auto revision_json = inventory::inventory_revision_json(revision);
    ASSERT_EQ(revision_json["entries"].size(), 2u);
    EXPECT_EQ(revision_json["entries"][0], inventory::inventory_entry_json(entry));
    EXPECT_EQ(revision_json["entries"][1], inventory::inventory_entry_json(deactivated));

    // 空的 optional 要序列化成 null，不能塌成缺字段——前端按 null 判断显示破折号。
    const auto first = inventory::inventory_entry_json(entry);
    EXPECT_TRUE(first["span_or_location"].isNull());
    EXPECT_TRUE(first["deactivated_at"].isNull());
    EXPECT_TRUE(first["remarks"].isNull());
    EXPECT_EQ(first["mappings"].size(), 1u);
    EXPECT_EQ(first["mappings"][0]["confirmation_status"].asString(), "已确认");

    const auto second = inventory::inventory_entry_json(deactivated);
    EXPECT_EQ(second["span_or_location"].asString(), "第 1 孔");
    EXPECT_EQ(second["deactivation_reason"].asString(), "现场已拆除");
    EXPECT_FALSE(second["is_active"].asBool());
}

TEST(ComponentInventoryModelsTest, LocatedEntryAddsOnlyPosition) {
    inventory::InventoryEntry entry;
    entry.id = "entry-1";
    entry.bridge_component_id = "component-1";
    entry.component_number = "33-2-50#支座";
    entry.site_name = "支座";
    entry.site_component_type = "支座";

    auto expected = inventory::inventory_entry_json(entry);
    const auto located = inventory::located_entry_json(entry, 3299);
    EXPECT_EQ(located["position"].asInt64(), 3299);

    expected["position"] = static_cast<Json::Int64>(3299);
    EXPECT_EQ(located, expected) << "组内序号之外不应有任何差异";
}
