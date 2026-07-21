#include <gtest/gtest.h>

#include "bridge_report/inventory/ComponentInventoryGenerator.hpp"

namespace inventory = bridge_report::inventory;

TEST(ComponentInventoryGeneratorTest, FiveSpansAndSixGirdersProduceThirtyEditableNumbers) {
    inventory::GenerateInventoryInput input;
    input.span_count = 5;
    inventory::GenerationGroupInput girders;
    girders.site_component_type = "主梁";
    girders.site_name = "主梁";
    girders.standard_component_category_id = "h21.component.beam.upper_bearing";
    girders.structure_part = "superstructure";
    girders.numbering_mode = inventory::NumberingMode::SpanMember;
    girders.quantity = 6;
    input.groups.push_back(girders);

    const auto result = inventory::generate_component_inventory(input);
    ASSERT_TRUE(result.ok()) << result.error_message;
    ASSERT_EQ(result.entries.size(), 30u);
    EXPECT_EQ(result.entries.front().component_number, "1-1#");
    EXPECT_EQ(result.entries.front().span_or_location, "第1跨");
    EXPECT_EQ(result.entries.back().component_number, "5-6#");
    EXPECT_EQ(result.entries.back().span_or_location, "第5跨");
}
TEST(ComponentInventoryGeneratorTest, CategoriesCanUseDifferentNumberingStrategies) {
    inventory::GenerateInventoryInput input;
    input.span_count = 2;
    input.groups.push_back({
        "主梁", "主梁", "h21.component.beam.upper_bearing", "superstructure",
        inventory::NumberingMode::SpanMember, 2, "L", "#"});
    input.groups.push_back({
        "桥墩", "桥墩", "h21.component.lower.pier", "substructure",
        inventory::NumberingMode::Sequential, 3, "P", ""});

    const auto result = inventory::generate_component_inventory(input);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.entries.size(), 7u);
    EXPECT_EQ(result.entries[0].component_number, "L1-1#");
    EXPECT_EQ(result.entries[4].component_number, "P1");
    EXPECT_EQ(result.entries[6].component_number, "P3");
}

TEST(ComponentInventoryGeneratorTest, PierLineNumberingUsesSpanCountMinusOne) {
    EXPECT_EQ(inventory::parse_numbering_mode("pier_line"), inventory::NumberingMode::PierLine);
    EXPECT_EQ(inventory::to_string(inventory::NumberingMode::PierLine), "pier_line");

    inventory::GenerateInventoryInput input;
    input.span_count = 5;
    input.groups.push_back({
        "桥墩", "桥墩", "h21.component.lower.pier", "substructure",
        inventory::NumberingMode::PierLine, 4, "", "#"});

    const auto result = inventory::generate_component_inventory(input);
    ASSERT_TRUE(result.ok()) << result.error_message;
    ASSERT_EQ(result.entries.size(), 16u);
    EXPECT_EQ(result.entries.front().component_number, "1-1#");
    EXPECT_EQ(result.entries.front().span_or_location, "第1墩位");
    EXPECT_EQ(result.entries.back().component_number, "4-4#");
    EXPECT_EQ(result.entries.back().span_or_location, "第4墩位");

    inventory::GenerateInventoryInput single_span;
    single_span.span_count = 1;
    single_span.groups.push_back({
        "桥墩", "桥墩", "h21.component.lower.pier", "substructure",
        inventory::NumberingMode::PierLine, 2, "", "#"});
    EXPECT_EQ(
        inventory::generate_component_inventory(single_span).error_code,
        "span_count_required_for_numbering");
}

TEST(ComponentInventoryGeneratorTest, InvalidCountsAndDuplicateNumbersAreRejected) {
    inventory::GenerateInventoryInput missing_span;
    missing_span.span_count = 0;
    missing_span.groups.push_back({
        "主梁", "主梁", "category", "superstructure",
        inventory::NumberingMode::SpanMember, 2, "", "#"});
    EXPECT_EQ(
        inventory::generate_component_inventory(missing_span).error_code,
        "span_count_required_for_numbering");

    inventory::GenerateInventoryInput duplicate;
    duplicate.span_count = 1;
    duplicate.groups.push_back({
        "主梁", "主梁A", "category.a", "superstructure",
        inventory::NumberingMode::Sequential, 1, "", "#"});
    duplicate.groups.push_back({
        "主梁", "主梁B", "category.b", "superstructure",
        inventory::NumberingMode::Sequential, 1, "", "#"});
    EXPECT_EQ(
        inventory::generate_component_inventory(duplicate).error_code,
        "duplicate_inventory_component_number");
}

TEST(ComponentInventoryGeneratorTest, GeneratesFromPartCatalog) {
    inventory::GenerateInventoryInput input;
    input.span_count = 5;
    inventory::PartSelection girders;
    girders.part_key = "beam.girder";
    girders.site_name = "空心板";  // 用户改名
    girders.counts = {13};         // 每孔 13 片
    input.part_selections.push_back(girders);

    inventory::PartSelection pavement;
    pavement.part_key = "deck.pavement";
    pavement.site_name = "桥面铺装";
    input.part_selections.push_back(pavement);

    const auto result = inventory::generate_component_inventory(input);
    ASSERT_TRUE(result.ok()) << result.error_message;
    ASSERT_EQ(result.entries.size(), 65u + 5u);
    EXPECT_EQ(result.entries.front().component_number, "1-1#空心板");  // 名称进类型词
    EXPECT_EQ(result.entries.front().site_component_type, "空心板");
    EXPECT_EQ(result.entries.front().standard_component_category_id,
              "h21.component.beam.upper_bearing");
    EXPECT_EQ(result.entries.front().structure_part, "superstructure");
    EXPECT_EQ(result.entries.front().span_or_location, "第1孔");
    EXPECT_EQ(result.entries[65].component_number, "1#跨桥面铺装");
}

TEST(ComponentInventoryGeneratorTest, RejectsUnknownPartAndEmptySelections) {
    inventory::GenerateInventoryInput unknown;
    unknown.span_count = 3;
    unknown.part_selections.push_back({"beam.nope", "?", {1}});
    EXPECT_EQ(inventory::generate_component_inventory(unknown).error_code, "unknown_part_key");

    inventory::GenerateInventoryInput empty;
    empty.span_count = 3;
    EXPECT_EQ(inventory::generate_component_inventory(empty).error_code,
              "inventory_groups_required");
}
