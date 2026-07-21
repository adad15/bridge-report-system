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
TEST(ComponentInventoryRoutesTest, UnknownCategoryIsNotSilentlyMappedToOther) {
    bridge_report::standards::StandardPackage package;
    bridge_report::standards::StandardDefinition template_definition;
    template_definition.id = "template.beam";
    template_definition.source_file = "inventory-templates.json";
    template_definition.payload["bridge_type_id"] = "bridge.beam";
    template_definition.payload["quantity_inputs"].append("span_count");
    template_definition.payload["quantity_inputs"].append("girder_count");
    package.definitions.emplace(template_definition.id, template_definition);

    bridge_report::standards::StandardDefinition category;
    category.id = "component.girder";
    category.source_file = "component-taxonomy.json";
    category.payload["bridge_type_ids"].append("bridge.beam");
    category.payload["structure_part"] = "superstructure";
    category.payload["generatable"] = true;
    package.definitions.emplace(category.id, category);

    inventory::GenerateInventoryInput input;
    input.template_id = "template.beam";
    input.bridge_type_id = "bridge.beam";
    input.span_count = 1;
    input.input_quantities["span_count"] = 1;
    input.input_quantities["girder_count"] = 1;
    input.groups.push_back({
        "自定义现场类型", "自定义名称", "unknown.category", "other",
        inventory::NumberingMode::Sequential, 1, "", "#"});
    input.groups[0].quantity_key = "girder_count";
    std::string code, message;
    EXPECT_FALSE(http::validate_inventory_generation_standard(input, package, code, message));
    EXPECT_EQ(code, "inventory_component_category_not_supported");

    input.groups[0].standard_component_category_id = "component.girder";
    input.groups[0].structure_part = "superstructure";
    EXPECT_TRUE(http::validate_inventory_generation_standard(input, package, code, message));

    input.input_quantities.removeMember("girder_count");
    EXPECT_FALSE(http::validate_inventory_generation_standard(input, package, code, message));
    EXPECT_EQ(code, "inventory_template_quantity_required");
}

TEST(ComponentInventoryRoutesTest, ExtraCategoryGroupsBeyondTemplateAreAllowed) {
    bridge_report::standards::StandardPackage package;
    bridge_report::standards::StandardDefinition template_definition;
    template_definition.id = "template.beam";
    template_definition.source_file = "inventory-templates.json";
    template_definition.payload["bridge_type_id"] = "bridge.beam";
    template_definition.payload["quantity_inputs"].append("span_count");
    template_definition.payload["quantity_inputs"].append("girder_count");
    package.definitions.emplace(template_definition.id, template_definition);

    bridge_report::standards::StandardDefinition girder;
    girder.id = "component.girder";
    girder.source_file = "component-taxonomy.json";
    girder.payload["bridge_type_ids"].append("bridge.beam");
    girder.payload["structure_part"] = "superstructure";
    girder.payload["generatable"] = true;
    package.definitions.emplace(girder.id, girder);

    bridge_report::standards::StandardDefinition pavement;
    pavement.id = "component.pavement";
    pavement.source_file = "component-taxonomy.json";
    pavement.payload["bridge_type_ids"].append("bridge.beam");
    pavement.payload["structure_part"] = "deck_system";
    pavement.payload["generatable"] = true;
    package.definitions.emplace(pavement.id, pavement);

    bridge_report::standards::StandardDefinition riverbed;
    riverbed.id = "component.riverbed";
    riverbed.source_file = "component-taxonomy.json";
    riverbed.payload["bridge_type_ids"].append("bridge.beam");
    riverbed.payload["structure_part"] = "substructure";
    riverbed.payload["generatable"] = false;
    package.definitions.emplace(riverbed.id, riverbed);

    inventory::GenerateInventoryInput input;
    input.template_id = "template.beam";
    input.bridge_type_id = "bridge.beam";
    input.span_count = 1;
    input.input_quantities["span_count"] = 1;
    input.input_quantities["girder_count"] = 2;
    input.input_quantities["component.pavement"] = 1;
    input.groups.push_back({
        "主梁", "主梁", "component.girder", "superstructure",
        inventory::NumberingMode::Sequential, 2, "", "#"});
    input.groups[0].quantity_key = "girder_count";
    input.groups.push_back({
        "桥面铺装", "桥面铺装", "component.pavement", "deck_system",
        inventory::NumberingMode::Sequential, 1, "", "#"});
    input.groups[1].quantity_key = "component.pavement";

    std::string code, message;
    EXPECT_TRUE(http::validate_inventory_generation_standard(input, package, code, message));

    // 额外分组的数量键必须等于该组的规范类别 ID。
    input.groups[1].quantity_key = "pavement_count";
    input.input_quantities["pavement_count"] = 1;
    EXPECT_FALSE(http::validate_inventory_generation_standard(input, package, code, message));
    EXPECT_EQ(code, "inventory_group_quantity_mismatch");
    input.input_quantities.removeMember("pavement_count");
    input.groups[1].quantity_key = "component.pavement";

    // 额外分组数量之和必须与 input_quantities 一致。
    input.input_quantities["component.pavement"] = 3;
    EXPECT_FALSE(http::validate_inventory_generation_standard(input, package, code, message));
    EXPECT_EQ(code, "inventory_group_quantity_mismatch");
    input.input_quantities["component.pavement"] = 1;

    // 额外类别可以拆成多个种类分组，数量之和等于数量项时通过。
    input.groups.push_back(input.groups[1]);
    input.groups[2].site_component_type = "人行道铺装";
    input.groups[2].site_name = "人行道铺装";
    EXPECT_FALSE(http::validate_inventory_generation_standard(input, package, code, message));
    EXPECT_EQ(code, "inventory_group_quantity_mismatch");
    input.input_quantities["component.pavement"] = 2;
    EXPECT_TRUE(http::validate_inventory_generation_standard(input, package, code, message));
    input.input_quantities["component.pavement"] = 1;
    input.groups.pop_back();

    // 模板数量项同样可以拆分为多个种类分组。
    input.groups[0].quantity = 1;
    auto second_kind = input.groups[0];
    second_kind.site_component_type = "横隔板";
    second_kind.site_name = "横隔板";
    input.groups.push_back(second_kind);
    EXPECT_TRUE(http::validate_inventory_generation_standard(input, package, code, message));

    // 之和不等于模板数量项时拒绝。
    input.groups.back().quantity = 2;
    EXPECT_FALSE(http::validate_inventory_generation_standard(input, package, code, message));
    EXPECT_EQ(code, "inventory_template_quantity_group_incomplete");
    input.groups.pop_back();
    input.groups[0].quantity = 2;

    // 不可生成的类别（如河床）仍被拒绝。
    input.groups[1].standard_component_category_id = "component.riverbed";
    input.groups[1].structure_part = "substructure";
    input.groups[1].quantity_key = "component.riverbed";
    input.input_quantities["component.riverbed"] = 1;
    EXPECT_FALSE(http::validate_inventory_generation_standard(input, package, code, message));
    EXPECT_EQ(code, "inventory_component_category_not_supported");
}

TEST(ComponentInventoryRoutesTest, GenerationParserRejectsInvalidQuantity) {
    Json::Value body;
    body["standard_package_id"] = "11111111-1111-1111-1111-111111111111";
    body["template_id"] = "template.beam";
    body["bridge_type_id"] = "bridge.beam";
    body["span_count"] = 5;
    Json::Value group;
    group["site_component_type"] = "主梁";
    group["site_name"] = "主梁";
    group["standard_component_category_id"] = "component.girder";
    group["structure_part"] = "superstructure";
    group["numbering_mode"] = "span_member";
    group["quantity"] = 0;
    group["quantity_key"] = "upper_bearing_members_per_span";
    body["groups"].append(group);
    inventory::GenerateInventoryInput output;
    std::string code, message;
    EXPECT_FALSE(inventory::parse_generate_inventory_input(body, output, code, message));
    EXPECT_EQ(code, "invalid_inventory_generation_group");
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
    EXPECT_TRUE(output.groups.empty());
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

    // 部件不在梁式桥目录。
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
