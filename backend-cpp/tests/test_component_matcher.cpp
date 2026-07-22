#include <gtest/gtest.h>

#include "bridge_report/inventory/ComponentMatcher.hpp"

namespace {

using bridge_report::inventory::ComponentMatchMethod;
using bridge_report::inventory::ConfirmedComponentAlias;
using bridge_report::inventory::DefectComponentText;
using bridge_report::inventory::InventoryEntry;
using bridge_report::inventory::InventoryMapping;
using bridge_report::inventory::InventoryRevision;
using bridge_report::inventory::match_defect_component;

// 台账条目：编号 + 现场名 + 活动映射的规范类别 id。
InventoryEntry entry(std::string id, std::string number, std::string site_name,
                     std::string category_id) {
    InventoryMapping mapping;
    mapping.id = "mapping-" + id;
    mapping.standard_component_category_id = std::move(category_id);
    mapping.structure_part = "superstructure";
    mapping.is_active = true;
    InventoryEntry value;
    value.id = "entry-" + id;
    value.bridge_component_id = std::move(id);
    value.component_number = std::move(number);
    value.site_name = site_name;
    value.site_component_type = std::move(site_name);
    value.mappings.push_back(std::move(mapping));
    return value;
}

InventoryRevision revision(std::vector<InventoryEntry> entries, std::string status = "已确认") {
    InventoryRevision value;
    value.id = "revision-1";
    value.bridge_id = "bridge-1";
    value.status = std::move(status);
    value.entries = std::move(entries);
    return value;
}

const char* kGirderCategory = "h21.component.beam.upper_bearing";
const char* kGeneralCategory = "h21.component.beam.upper_general";

TEST(ComponentMatcherTest, CategoryPlusNumberUniqueMatchBinds) {
    const auto result = match_defect_component(
        {"1-1#梁", "上部承重构件"},
        revision({entry("c1", "1-1#梁", "空心板", kGirderCategory)}), {});
    ASSERT_TRUE(result.matched_entry.has_value());
    EXPECT_EQ(result.method, ComponentMatchMethod::Exact);
    EXPECT_EQ(result.matched_entry->bridge_component_id, "c1");
    EXPECT_EQ(result.matched_mapping->standard_component_category_id, kGirderCategory);
}

TEST(ComponentMatcherTest, SiteNameDoesNotAffectMatch) {
    // 台账现场名与报告构件名都不参与匹配：类别 + 编号一致即命中。
    const auto result = match_defect_component(
        {"1-1#梁", "上部承重构件"},
        revision({entry("c1", "1-1#梁", "随便改的名字", kGirderCategory)}), {});
    ASSERT_TRUE(result.matched_entry.has_value());
    EXPECT_EQ(result.matched_entry->bridge_component_id, "c1");
}

TEST(ComponentMatcherTest, WrongCategoryDoesNotMatch) {
    const auto result = match_defect_component(
        {"1-1#梁", "支座"},  // 支座 → bearing 类别，与台账 upper_bearing 不符
        revision({entry("c1", "1-1#梁", "梁", kGirderCategory)}), {});
    EXPECT_FALSE(result.matched_entry.has_value());
    EXPECT_TRUE(result.candidate_component_ids.empty());
}

TEST(ComponentMatcherTest, TypeWordMustMatch) {
    // 报告类型词"板"与台账"梁"不一致 → 不自动命中（落人工绑定）。
    const auto result = match_defect_component(
        {"1-1#板", "上部承重构件"},
        revision({entry("c1", "1-1#梁", "梁", kGirderCategory)}), {});
    EXPECT_FALSE(result.matched_entry.has_value());
    EXPECT_TRUE(result.candidate_component_ids.empty());
}

TEST(ComponentMatcherTest, MultiComponentPartSeparatedByTypeWord) {
    // 同类别（上部一般构件）下湿接缝与横隔梁靠编号里的类型词/层级天然区分。
    const auto result = match_defect_component(
        {"1-1#湿接缝", "上部一般构件"},
        revision({entry("c2", "1-1#湿接缝", "湿接缝", kGeneralCategory),
                  entry("c3", "1-1-1#横隔梁", "横隔梁", kGeneralCategory)}), {});
    ASSERT_TRUE(result.matched_entry.has_value());
    EXPECT_EQ(result.matched_entry->bridge_component_id, "c2");
}

TEST(ComponentMatcherTest, FullWidthNormalizationStillExact) {
    const auto result = match_defect_component(
        {" 1－1＃梁 ", "上部承重构件"},  // 全角 + 空白，归一化后保留类型词"梁"
        revision({entry("c1", "1-1#梁", "梁", kGirderCategory)}), {});
    ASSERT_TRUE(result.matched_entry.has_value());
    EXPECT_EQ(result.matched_entry->bridge_component_id, "c1");
}

TEST(ComponentMatcherTest, MultipleCandidatesNeverSelectSilently) {
    const auto result = match_defect_component(
        {"1-1#梁", "上部承重构件"},
        revision({entry("c1", "1-1#梁", "梁", kGirderCategory),
                  entry("c2", " 1－1＃梁", "梁", kGirderCategory)}), {});
    EXPECT_FALSE(result.matched_entry.has_value());
    ASSERT_EQ(result.candidate_component_ids.size(), 2u);
}

TEST(ComponentMatcherTest, UnconfirmedInventoryOnlyProvidesCandidates) {
    const auto result = match_defect_component(
        {"1-1#梁", "上部承重构件"},
        revision({entry("c1", "1-1#梁", "梁", kGirderCategory)}, "draft"), {});
    EXPECT_FALSE(result.matched_entry.has_value());
    EXPECT_EQ(result.candidate_component_ids, std::vector<std::string>{"c1"});
}

TEST(ComponentMatcherTest, ConfirmedAliasBindsWhenPartNameUnknown) {
    // 报告部件名称不在对照表 → 主路径无候选；已确认别名兜底自动绑定。
    const auto result = match_defect_component(
        {"1-1#梁", "老图纸叫法"},
        revision({entry("c1", "1-1#梁", "梁", kGirderCategory)}),
        {ConfirmedComponentAlias{"c1", "老图纸叫法"}});
    ASSERT_TRUE(result.matched_entry.has_value());
    EXPECT_EQ(result.method, ComponentMatchMethod::ConfirmedAlias);
}

TEST(ComponentMatcherTest, FuzzyTextDoesNotMatch) {
    const auto result = match_defect_component(
        {"大概1号梁", "上部承重构件"},
        revision({entry("c1", "1-1#梁", "梁", kGirderCategory)}), {});
    EXPECT_FALSE(result.matched_entry.has_value());
    EXPECT_TRUE(result.candidate_component_ids.empty());
}

}  // namespace
