#include <gtest/gtest.h>

#include "bridge_report/inventory/BeamBridgePartCatalog.hpp"

namespace nt = bridge_report::inventory;

TEST(BeamCatalogTest, HasEighteenPartsWithStableKeys) {
    const auto& parts = nt::beam_bridge_parts();
    EXPECT_GE(parts.size(), 18u);

    const auto* girder = nt::find_part(parts, "beam.girder");
    ASSERT_NE(girder, nullptr);
    EXPECT_EQ(girder->default_name, "梁");
    EXPECT_EQ(girder->standard_component_category_id, "h21.component.beam.upper_bearing");
    EXPECT_EQ(girder->structure_part, "superstructure");
    EXPECT_EQ(girder->number_template, "{span}-{c1}#{name}");

    EXPECT_EQ(nt::find_part(parts, "no.such.part"), nullptr);
}

TEST(BeamCatalogTest, GirderGeneratesDocForms) {
    const auto* girder = nt::find_part(nt::beam_bridge_parts(), "beam.girder");
    ASSERT_NE(girder, nullptr);
    const auto tpl = girder->number_template_with("梁", {13});
    const auto out = nt::expand(tpl, nt::NumberingContext{5});
    ASSERT_EQ(out.size(), 65u);
    EXPECT_EQ(out.front().number, "1-1#梁");
    EXPECT_EQ(out.back().number, "5-13#梁");
}

TEST(BeamCatalogTest, RenamedGirderFlowsIntoNumber) {
    const auto* girder = nt::find_part(nt::beam_bridge_parts(), "beam.girder");
    ASSERT_NE(girder, nullptr);
    const auto out =
        nt::expand(girder->number_template_with("空心板", {2}), nt::NumberingContext{2});
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out.front().number, "1-1#空心板");
    EXPECT_EQ(out.back().number, "2-2#空心板");
}

TEST(BeamCatalogTest, EmptyNameFallsBackToDefault) {
    const auto* cap = nt::find_part(nt::beam_bridge_parts(), "lower.pier_cap");
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(cap->number_template, "{pier}#墩{name}");
    const auto out = nt::expand(cap->number_template_with("", {}), nt::NumberingContext{3});
    ASSERT_EQ(out.size(), 2u);  // 梁式桥墩数 = 跨数 - 1
    EXPECT_EQ(out.front().number, "1#墩盖梁");
}

TEST(BeamCatalogTest, DeckPavementAndFoundationFollowDoc) {
    const auto* pav = nt::find_part(nt::beam_bridge_parts(), "deck.pavement");
    ASSERT_NE(pav, nullptr);
    EXPECT_EQ(pav->number_template, "{span}#跨{name}");
    const auto pav_out =
        nt::expand(pav->number_template_with("桥面铺装", {}), nt::NumberingContext{5});
    ASSERT_EQ(pav_out.size(), 5u);
    EXPECT_EQ(pav_out.back().number, "5#跨桥面铺装");

    const auto* base = nt::find_part(nt::beam_bridge_parts(), "lower.foundation");
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->number_template, "{line}{name}");
    const auto base_out =
        nt::expand(base->number_template_with("基础", {}), nt::NumberingContext{5});
    EXPECT_EQ(base_out.front().number, "0#台基础");
}
