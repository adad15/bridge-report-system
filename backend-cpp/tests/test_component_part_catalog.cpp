#include <gtest/gtest.h>

#include "bridge_report/inventory/ComponentPartCatalog.hpp"

namespace nt = bridge_report::inventory;

TEST(PartCatalogTest, KeepsBeamPartsWithStableKeys) {
    const auto& parts = nt::component_parts();

    const auto* girder = nt::find_part(parts, "beam.girder");
    ASSERT_NE(girder, nullptr);
    EXPECT_EQ(girder->default_name, "梁");
    EXPECT_EQ(girder->standard_component_category_id, "h21.component.beam.upper_bearing");
    EXPECT_EQ(girder->structure_part, "superstructure");
    EXPECT_EQ(girder->number_template, "{span}-{c1}#{name}");
    EXPECT_FALSE(girder->provisional);

    EXPECT_EQ(nt::find_part(parts, "no.such.part"), nullptr);
}

TEST(PartCatalogTest, CoversEveryBridgeTypeSuperstructureAndSharedGap) {
    const auto& parts = nt::component_parts();
    // 各桥型上部代表部件 + 补漏的调治构造物 + 共享支座都在目录中。
    for (const char* key : {"bearing.support", "lower.regulation", "arch.main_ring",
                            "arch.segment", "carch.rib", "cs.tower", "cs.cable",
                            "sp.main_cable", "sp.anchorage"})
        EXPECT_NE(nt::find_part(parts, key), nullptr) << key;
}

TEST(PartCatalogTest, GirderGeneratesDocForms) {
    const auto* girder = nt::find_part(nt::component_parts(), "beam.girder");
    ASSERT_NE(girder, nullptr);
    const auto out = nt::expand(girder->number_template_with("梁", {13}), nt::NumberingContext{5});
    ASSERT_EQ(out.size(), 65u);
    EXPECT_EQ(out.front().number, "1-1#梁");
    EXPECT_EQ(out.back().number, "5-13#梁");
}

TEST(PartCatalogTest, RenamedGirderFlowsIntoNumber) {
    const auto* girder = nt::find_part(nt::component_parts(), "beam.girder");
    ASSERT_NE(girder, nullptr);
    const auto out =
        nt::expand(girder->number_template_with("空心板", {2}), nt::NumberingContext{2});
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out.front().number, "1-1#空心板");
    EXPECT_EQ(out.back().number, "2-2#空心板");
}

TEST(PartCatalogTest, EmptyNameFallsBackToDefault) {
    const auto* cap = nt::find_part(nt::component_parts(), "lower.pier_cap");
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(cap->number_template, "{pier}#墩{name}");
    const auto out = nt::expand(cap->number_template_with("", {}), nt::NumberingContext{3});
    ASSERT_EQ(out.size(), 2u);  // 墩数 = 跨数 - 1
    EXPECT_EQ(out.front().number, "1#墩盖梁");
}

TEST(PartCatalogTest, DeckPavementAndFoundationFollowDoc) {
    const auto* pav = nt::find_part(nt::component_parts(), "deck.pavement");
    ASSERT_NE(pav, nullptr);
    EXPECT_EQ(pav->number_template, "{span}#跨{name}");
    const auto pav_out =
        nt::expand(pav->number_template_with("桥面铺装", {}), nt::NumberingContext{5});
    ASSERT_EQ(pav_out.size(), 5u);
    EXPECT_EQ(pav_out.back().number, "5#跨桥面铺装");

    const auto* base = nt::find_part(nt::component_parts(), "lower.foundation");
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->number_template, "{line}{name}");
    const auto base_out =
        nt::expand(base->number_template_with("基础", {}), nt::NumberingContext{5});
    EXPECT_EQ(base_out.front().number, "0#台基础");
}

TEST(PartCatalogTest, ProvisionalSuperstructureExpandsSanely) {
    const auto* ring = nt::find_part(nt::component_parts(), "arch.main_ring");
    ASSERT_NE(ring, nullptr);
    EXPECT_TRUE(ring->provisional);
    const auto ring_out =
        nt::expand(ring->number_template_with("主拱圈", {2}), nt::NumberingContext{3});
    ASSERT_EQ(ring_out.size(), 6u);  // 3 孔 × 2
    EXPECT_EQ(ring_out.front().number, "1-1#主拱圈");

    const auto* cable = nt::find_part(nt::component_parts(), "sp.main_cable");
    ASSERT_NE(cable, nullptr);
    EXPECT_TRUE(cable->provisional);
    const auto cable_out =
        nt::expand(cable->number_template_with("主缆", {}), nt::NumberingContext{4});
    ASSERT_EQ(cable_out.size(), 2u);  // 左 / 右
    EXPECT_EQ(cable_out.front().number, "左侧主缆");
    EXPECT_EQ(cable_out.back().number, "右侧主缆");

    const auto* tower = nt::find_part(nt::component_parts(), "cs.tower");
    ASSERT_NE(tower, nullptr);
    const auto tower_out =
        nt::expand(tower->number_template_with("索塔", {2}), nt::NumberingContext{5});
    ASSERT_EQ(tower_out.size(), 2u);  // 纯序号，与孔数无关
    EXPECT_EQ(tower_out.front().number, "1#索塔");
    EXPECT_EQ(tower_out.back().number, "2#索塔");
}
