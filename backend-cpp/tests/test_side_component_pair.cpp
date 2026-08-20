#include <gtest/gtest.h>

#include "bridge_report/inventory/SideComponentPair.hpp"

namespace {

using bridge_report::inventory::find_side_component_pair;
using bridge_report::inventory::InventoryEntry;
using bridge_report::inventory::InventoryMapping;
using bridge_report::inventory::InventoryRevision;
using bridge_report::inventory::side_pair_category_allowed;

constexpr const char* kRailing = "h21.component.deck.railing";
constexpr const char* kSidewalk = "h21.component.deck.sidewalk";
constexpr const char* kWingWall = "h21.component.lower.wing_or_ear_wall";
constexpr const char* kConeSlope = "h21.component.lower.cone_or_protection_slope";

InventoryEntry entry(std::string id, std::string number, std::string category_id,
                     bool is_active = true, bool has_active_mapping = true) {
    InventoryMapping mapping;
    mapping.id = "mapping-" + id;
    mapping.standard_component_category_id = std::move(category_id);
    mapping.structure_part = "deck_system";
    mapping.is_active = has_active_mapping;
    InventoryEntry value;
    value.id = "entry-" + id;
    value.bridge_component_id = std::move(id);
    value.component_number = std::move(number);
    value.site_component_type = "构件";
    value.is_active = is_active;
    value.mappings.push_back(std::move(mapping));
    return value;
}

InventoryRevision revision(std::vector<InventoryEntry> entries) {
    InventoryRevision value;
    value.id = "revision-1";
    value.bridge_id = "bridge-1";
    value.status = "已确认";
    value.entries = std::move(entries);
    return value;
}

// 百股大桥的栏杆台账：{side}侧{name} 展开出的两件。
std::vector<InventoryEntry> railing_pair() {
    return {entry("c-left", "左侧栏杆", kRailing), entry("c-right", "右侧栏杆", kRailing)};
}

TEST(SideComponentPairTest, FindsTheRailingPair) {
    const auto pair = find_side_component_pair(revision(railing_pair()), kRailing);
    ASSERT_TRUE(pair.has_value());
    // 左右由编号里的"左/右"定，不能靠台账顺序——顺序换了结论必须不变。
    EXPECT_EQ(pair->left_bridge_component_id, "c-left");
    EXPECT_EQ(pair->right_bridge_component_id, "c-right");
    EXPECT_EQ(pair->left_component_number, "左侧栏杆");
    EXPECT_EQ(pair->right_component_number, "右侧栏杆");
}

TEST(SideComponentPairTest, IsIndifferentToEntryOrder) {
    auto entries = railing_pair();
    std::swap(entries[0], entries[1]);
    const auto pair = find_side_component_pair(revision(entries), kRailing);
    ASSERT_TRUE(pair.has_value());
    EXPECT_EQ(pair->left_bridge_component_id, "c-left");
    EXPECT_EQ(pair->right_bridge_component_id, "c-right");
}

TEST(SideComponentPairTest, FindsTheSidewalkPair) {
    const auto pair = find_side_component_pair(
        revision({entry("s-left", "左侧人行道", kSidewalk),
                  entry("s-right", "右侧人行道", kSidewalk)}),
        kSidewalk);
    ASSERT_TRUE(pair.has_value());
    EXPECT_EQ(pair->left_bridge_component_id, "s-left");
    EXPECT_EQ(pair->right_bridge_component_id, "s-right");
}

TEST(SideComponentPairTest, IgnoresComponentsOfOtherCategories) {
    auto entries = railing_pair();
    entries.push_back(entry("s-left", "左侧人行道", kSidewalk));
    entries.push_back(entry("s-right", "右侧人行道", kSidewalk));
    const auto pair = find_side_component_pair(revision(entries), kRailing);
    ASSERT_TRUE(pair.has_value()) << "别的类别的构件不该把栏杆的件数顶超";
    EXPECT_EQ(pair->left_bridge_component_id, "c-left");
}

// 锥坡类别下有 4 件锥坡 + 2 件护坡，共 6 件、两对，"两侧"指哪个台是含糊的。
TEST(SideComponentPairTest, RejectsTheConeSlopeCategory) {
    const auto pair = find_side_component_pair(
        revision({entry("k1", "0#台左侧锥坡", kConeSlope),
                  entry("k2", "0#台右侧锥坡", kConeSlope),
                  entry("k3", "33#台左侧锥坡", kConeSlope),
                  entry("k4", "33#台右侧锥坡", kConeSlope),
                  entry("k5", "0#台护坡", kConeSlope),
                  entry("k6", "33#台护坡", kConeSlope)}),
        kConeSlope);
    EXPECT_FALSE(pair.has_value());
}

TEST(SideComponentPairTest, RejectsTheWingWallCategory) {
    const auto pair = find_side_component_pair(
        revision({entry("w1", "0#台左侧翼墙", kWingWall),
                  entry("w2", "0#台右侧翼墙", kWingWall),
                  entry("w3", "33#台左侧翼墙", kWingWall),
                  entry("w4", "33#台右侧翼墙", kWingWall)}),
        kWingWall);
    EXPECT_FALSE(pair.has_value());
}

// 这条是放行名单存在的理由，必须单独锁住：结构上确实成一对，但类别不在名单内，
// 仍然不给选项。去掉名单、只留结构判定的话，只有这条会变红。
TEST(SideComponentPairTest, StillRejectsWingWallsThatHappenToFormASinglePair) {
    const auto entries = std::vector<InventoryEntry>{
        entry("w1", "0#台左侧翼墙", kWingWall), entry("w2", "0#台右侧翼墙", kWingWall)};
    ASSERT_FALSE(side_pair_category_allowed(kWingWall))
        << "前提：翼墙不在放行名单内";
    EXPECT_FALSE(find_side_component_pair(revision(entries), kWingWall).has_value())
        << "结构上成对，但放行范围应当由名单决定，不由某座桥勾了什么决定";
}

TEST(SideComponentPairTest, RejectsWhenOnlyOneSideWasGenerated) {
    const auto pair = find_side_component_pair(
        revision({entry("c-left", "左侧栏杆", kRailing)}), kRailing);
    EXPECT_FALSE(pair.has_value());
}

TEST(SideComponentPairTest, RejectsTwoComponentsThatAreNotALeftRightPair) {
    // 件数对上了，但两个编号不是同一个记号的左右两面。
    const auto pair = find_side_component_pair(
        revision({entry("c1", "左侧栏杆", kRailing), entry("c2", "右侧扶手", kRailing)}),
        kRailing);
    EXPECT_FALSE(pair.has_value());
}

TEST(SideComponentPairTest, RejectsTwoComponentsOnTheSameSide) {
    const auto pair = find_side_component_pair(
        revision({entry("c1", "左侧栏杆", kRailing), entry("c2", "左侧扶手", kRailing)}),
        kRailing);
    EXPECT_FALSE(pair.has_value());
}

TEST(SideComponentPairTest, DeactivatedComponentsDoNotCount) {
    auto entries = railing_pair();
    entries.push_back(entry("c-old", "左侧旧栏杆", kRailing, /*is_active=*/false));
    const auto pair = find_side_component_pair(revision(entries), kRailing);
    ASSERT_TRUE(pair.has_value()) << "停用件不该把件数顶到 3 件";
    EXPECT_EQ(pair->left_bridge_component_id, "c-left");
}

TEST(SideComponentPairTest, ComponentsWithoutAnActiveMappingDoNotCount) {
    auto entries = railing_pair();
    entries.push_back(
        entry("c-nomap", "左侧旧栏杆", kRailing, /*is_active=*/true, /*has_active_mapping=*/false));
    const auto pair = find_side_component_pair(revision(entries), kRailing);
    ASSERT_TRUE(pair.has_value()) << "无生效映射的构件与匹配口径一致，不计入";
    EXPECT_EQ(pair->left_bridge_component_id, "c-left");
}

TEST(SideComponentPairTest, RejectsWhenTheOnlyPairMemberIsDeactivated) {
    const auto pair = find_side_component_pair(
        revision({entry("c-left", "左侧栏杆", kRailing),
                  entry("c-right", "右侧栏杆", kRailing, /*is_active=*/false)}),
        kRailing);
    EXPECT_FALSE(pair.has_value());
}

TEST(SideComponentPairTest, AllowlistHoldsExactlySidewalkAndRailing) {
    EXPECT_TRUE(side_pair_category_allowed(kSidewalk));
    EXPECT_TRUE(side_pair_category_allowed(kRailing));
    EXPECT_FALSE(side_pair_category_allowed(kWingWall));
    EXPECT_FALSE(side_pair_category_allowed(kConeSlope));
    EXPECT_FALSE(side_pair_category_allowed("h21.component.beam.upper_bearing"));
    EXPECT_FALSE(side_pair_category_allowed(""));
}

}  // namespace
