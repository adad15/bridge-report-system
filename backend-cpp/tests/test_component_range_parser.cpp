#include <gtest/gtest.h>

#include "bridge_report/inventory/ComponentRangeParser.hpp"

namespace {

using bridge_report::inventory::ComponentRangeParseStatus;
using bridge_report::inventory::parse_component_range;

TEST(ComponentRangeParserTest, ExpandsAsciiWaveRange) {
    const auto result = parse_component_range("1-1#板~1-25#板");
    ASSERT_EQ(result.status, ComponentRangeParseStatus::Ok);
    ASSERT_EQ(result.numbers.size(), 25u);
    EXPECT_EQ(result.numbers.front(), "1-1#板");
    EXPECT_EQ(result.numbers.back(), "1-25#板");
}

TEST(ComponentRangeParserTest, ExpandsFullWidthWaveAndTrimsSeparatorSpaces) {
    const auto result = parse_component_range("3-2#铰缝 ～ 3-5#铰缝");
    ASSERT_EQ(result.status, ComponentRangeParseStatus::Ok);
    EXPECT_EQ(result.numbers, (std::vector<std::string>{
        "3-2#铰缝", "3-3#铰缝", "3-4#铰缝", "3-5#铰缝"}));
}

TEST(ComponentRangeParserTest, UsesStartEndpointDisplayTemplate) {
    const auto result = parse_component_range(" 2－01＃板 ~ 2-3#板 ");
    ASSERT_EQ(result.status, ComponentRangeParseStatus::Ok);
    EXPECT_EQ(result.numbers, (std::vector<std::string>{
        "2－01＃板", "2－02＃板", "2－03＃板"}));
}

TEST(ComponentRangeParserTest, RejectsDifferentPrefixOrSuffix) {
    EXPECT_EQ(parse_component_range("1-1#板~2-25#板").status,
              ComponentRangeParseStatus::Invalid);
    EXPECT_EQ(parse_component_range("1-1#板~1-25#梁").status,
              ComponentRangeParseStatus::Invalid);
}

TEST(ComponentRangeParserTest, RejectsDescendingOrSingleValueRanges) {
    EXPECT_EQ(parse_component_range("1-25#板~1-1#板").status,
              ComponentRangeParseStatus::Invalid);
    EXPECT_EQ(parse_component_range("1-1#板~1-1#板").status,
              ComponentRangeParseStatus::Invalid);
}

TEST(ComponentRangeParserTest, RejectsMissingOrMultipleSeparators) {
    EXPECT_EQ(parse_component_range("1-1#板").status,
              ComponentRangeParseStatus::NotRange);
    EXPECT_EQ(parse_component_range("1-1#板~1-2#板~1-3#板").status,
              ComponentRangeParseStatus::Invalid);
}

TEST(ComponentRangeParserTest, RejectsEndpointsWithoutTrailingIntegerSlot) {
    EXPECT_EQ(parse_component_range("一号板~二号板").status,
              ComponentRangeParseStatus::Invalid);
}

TEST(ComponentRangeParserTest, EnforcesExpansionLimit) {
    const auto result = parse_component_range("1-1#板~1-501#板", 500);
    EXPECT_EQ(result.status, ComponentRangeParseStatus::LimitExceeded);
    EXPECT_TRUE(result.numbers.empty());
}

}  // namespace
