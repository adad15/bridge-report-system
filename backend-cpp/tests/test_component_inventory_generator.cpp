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
