#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bridge_report/inventory/NumberingTemplate.hpp"

namespace nt = bridge_report::inventory;

TEST(NumberingRangeTest, SkeletonRangesFollowTopology) {
    const auto ctx = nt::NumberingContext{/*span_count=*/5};
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Span, 0),
              (std::vector<nt::PlaceValue>{{"1", "第1孔"}, {"2", "第2孔"}, {"3", "第3孔"},
                                           {"4", "第4孔"}, {"5", "第5孔"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Pier, 0),
              (std::vector<nt::PlaceValue>{{"1", "第1墩"}, {"2", "第2墩"},
                                           {"3", "第3墩"}, {"4", "第4墩"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Abutment, 0),
              (std::vector<nt::PlaceValue>{{"0", "第0台"}, {"5", "第5台"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::SupportLine, 0),
              (std::vector<nt::PlaceValue>{{"0#台", "0#台"}, {"1#墩", "1#墩"}, {"2#墩", "2#墩"},
                                           {"3#墩", "3#墩"}, {"4#墩", "4#墩"}, {"5#台", "5#台"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Side, 0),
              (std::vector<nt::PlaceValue>{{"左", "左侧"}, {"右", "右侧"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Count, 3),
              (std::vector<nt::PlaceValue>{{"1", ""}, {"2", ""}, {"3", ""}}));
}

TEST(NumberingExpandTest, TwoLevelSpanMember) {
    nt::NumberingTemplate tpl;
    tpl.pattern = "{span}-{c1}#梁";
    tpl.slots = {{"{span}", nt::Placeholder::Span, 0}, {"{c1}", nt::Placeholder::Count, 13}};
    const auto out = nt::expand(tpl, nt::NumberingContext{5});
    ASSERT_EQ(out.size(), 65u);
    EXPECT_EQ(out.front().number, "1-1#梁");
    EXPECT_EQ(out.front().location, "第1孔");
    EXPECT_EQ(out.back().number, "5-13#梁");
    EXPECT_EQ(out.back().location, "第5孔");
}

TEST(NumberingExpandTest, ThreeLevelDiaphragm) {
    nt::NumberingTemplate tpl;
    tpl.pattern = "{span}-{c1}-{c2}#横隔梁";
    tpl.slots = {{"{span}", nt::Placeholder::Span, 0},
                 {"{c1}", nt::Placeholder::Count, 12},
                 {"{c2}", nt::Placeholder::Count, 2}};
    const auto out = nt::expand(tpl, nt::NumberingContext{5});
    ASSERT_EQ(out.size(), 5u * 12u * 2u);
    EXPECT_EQ(out.front().number, "1-1-1#横隔梁");
    EXPECT_EQ(out.back().number, "5-12-2#横隔梁");
}

TEST(NumberingExpandTest, SupportLineAndAbutmentAndSide) {
    nt::NumberingTemplate base;
    base.pattern = "{line}基础";
    base.slots = {{"{line}", nt::Placeholder::SupportLine, 0}};
    const auto base_out = nt::expand(base, nt::NumberingContext{5});
    ASSERT_EQ(base_out.size(), 6u);
    EXPECT_EQ(base_out.front().number, "0#台基础");
    EXPECT_EQ(base_out[1].number, "1#墩基础");
    EXPECT_EQ(base_out.back().number, "5#台基础");

    nt::NumberingTemplate wing;
    wing.pattern = "{ab}#台{side}侧翼墙";
    wing.slots = {{"{ab}", nt::Placeholder::Abutment, 0}, {"{side}", nt::Placeholder::Side, 0}};
    const auto wing_out = nt::expand(wing, nt::NumberingContext{5});
    ASSERT_EQ(wing_out.size(), 4u);
    EXPECT_EQ(wing_out.front().number, "0#台左侧翼墙");
    EXPECT_EQ(wing_out.back().number, "5#台右侧翼墙");

    nt::NumberingTemplate walk;
    walk.pattern = "{side}侧人行道";
    walk.slots = {{"{side}", nt::Placeholder::Side, 0}};
    const auto walk_out = nt::expand(walk, nt::NumberingContext{5});
    ASSERT_EQ(walk_out.size(), 2u);
    EXPECT_EQ(walk_out.front().number, "左侧人行道");
}

TEST(NumberingExpandTest, WholeBridgeNoPlaceholder) {
    nt::NumberingTemplate tpl;
    tpl.pattern = "排水系统";
    tpl.slots = {};
    const auto out = nt::expand(tpl, nt::NumberingContext{5});
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out.front().number, "排水系统");
    EXPECT_EQ(out.front().location, "");
}
