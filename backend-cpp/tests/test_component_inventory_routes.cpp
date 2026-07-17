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
