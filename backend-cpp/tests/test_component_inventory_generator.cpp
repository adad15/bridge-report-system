#include <gtest/gtest.h>

#include "bridge_report/inventory/ComponentInventoryGenerator.hpp"

namespace inventory = bridge_report::inventory;

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

// 翼墙几何上是 2 台 × 2 侧 = 4 个，但真实桥常缺其中几处，用户去掉的不生成。
TEST(ComponentInventoryGeneratorTest, DropsExcludedInstances) {
    inventory::GenerateInventoryInput input;
    input.span_count = 33;
    inventory::PartSelection wing;
    wing.part_key = "lower.wing_wall";
    wing.site_name = "翼墙";
    wing.excluded_numbers = {"0#台右侧翼墙", "33#台左侧翼墙"};
    input.part_selections.push_back(wing);

    const auto result = inventory::generate_component_inventory(input);
    ASSERT_TRUE(result.ok()) << result.error_message;
    ASSERT_EQ(result.entries.size(), 2u);
    EXPECT_EQ(result.entries.front().component_number, "0#台左侧翼墙");
    EXPECT_EQ(result.entries.back().component_number, "33#台右侧翼墙");
}

// 排除项对不上展开结果说明前后端不一致，必须报错而不是静默忽略。
TEST(ComponentInventoryGeneratorTest, RejectsExcludedNumberOutsideExpansion) {
    inventory::GenerateInventoryInput input;
    input.span_count = 33;
    inventory::PartSelection slope;
    slope.part_key = "lower.protection_slope";
    slope.site_name = "护坡";
    slope.excluded_numbers = {"7#台护坡"};  // 只会展开 0#/33#
    input.part_selections.push_back(slope);

    EXPECT_EQ(inventory::generate_component_inventory(input).error_code,
              "unknown_excluded_component_number");
}

TEST(ComponentInventoryGeneratorTest, RejectsUnknownPartAndEmptySelections) {
    inventory::GenerateInventoryInput unknown;
    unknown.span_count = 3;
    unknown.part_selections.push_back({"beam.nope", "?", {1}});
    EXPECT_EQ(inventory::generate_component_inventory(unknown).error_code, "unknown_part_key");

    inventory::GenerateInventoryInput empty;
    empty.span_count = 3;
    EXPECT_EQ(inventory::generate_component_inventory(empty).error_code,
              "inventory_part_selections_required");
}
