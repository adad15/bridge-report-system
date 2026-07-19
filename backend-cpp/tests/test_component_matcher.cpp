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

InventoryEntry entry(std::string id, std::string number, std::string type) {
    InventoryMapping mapping;
    mapping.id = "mapping-" + id;
    mapping.standard_component_category_id = "category-" + id;
    mapping.structure_part = "superstructure";
    mapping.is_active = true;
    InventoryEntry value;
    value.id = "entry-" + id;
    value.bridge_component_id = std::move(id);
    value.component_number = std::move(number);
    value.site_name = type;
    value.site_component_type = std::move(type);
    value.mappings.push_back(std::move(mapping));
    return value;
}

InventoryRevision revision(std::vector<InventoryEntry> entries, std::string status = "confirmed") {
    InventoryRevision value;
    value.id = "revision-1";
    value.bridge_id = "bridge-1";
    value.status = std::move(status);
    value.entries = std::move(entries);
    return value;
}

TEST(ComponentMatcherTest, UniqueExactNumberAndTypeMatchAssociatesAutomatically) {
    const auto result = match_defect_component(
        DefectComponentText{"1-1#", "主梁"}, revision({entry("component-1", "1-1#", "主梁")}), {});

    ASSERT_TRUE(result.matched_entry.has_value());
    EXPECT_EQ(result.method, ComponentMatchMethod::Exact);
    EXPECT_EQ(result.matched_entry->bridge_component_id, "component-1");
    EXPECT_EQ(result.matched_mapping->standard_component_category_id, "category-component-1");
}

TEST(ComponentMatcherTest, ConfirmedNameAliasAssociatesAutomatically) {
    const auto result = match_defect_component(
        DefectComponentText{"1-1#", "上部承重构件"},
        revision({entry("component-1", "1-1#", "主梁")}),
        {ConfirmedComponentAlias{"component-1", "上部承重构件"}});

    ASSERT_TRUE(result.matched_entry.has_value());
    EXPECT_EQ(result.method, ComponentMatchMethod::ConfirmedAlias);
}

TEST(ComponentMatcherTest, NormalizedNumberOnlyProducesCandidates) {
    const auto result = match_defect_component(
        DefectComponentText{" 1－1＃ ", "主梁"}, revision({entry("component-1", "1-1#", "主梁")}), {});

    EXPECT_FALSE(result.matched_entry.has_value());
    EXPECT_EQ(result.method, ComponentMatchMethod::NormalizedCandidate);
    EXPECT_EQ(result.candidate_component_ids, std::vector<std::string>{"component-1"});
}

TEST(ComponentMatcherTest, MultipleCandidatesNeverSelectSilently) {
    const auto result = match_defect_component(
        DefectComponentText{"1－1#", "主梁"},
        revision({entry("component-1", "1-1", "主梁"), entry("component-2", "1-1#", "主梁")}), {});

    EXPECT_FALSE(result.matched_entry.has_value());
    ASSERT_EQ(result.candidate_component_ids.size(), 2u);
}

TEST(ComponentMatcherTest, FuzzyTextDoesNotMatch) {
    const auto result = match_defect_component(
        DefectComponentText{"大概1号梁", "梁"}, revision({entry("component-1", "1-1#", "主梁")}), {});

    EXPECT_FALSE(result.matched_entry.has_value());
    EXPECT_TRUE(result.candidate_component_ids.empty());
}

TEST(ComponentMatcherTest, UnconfirmedInventoryOnlyProvidesCandidates) {
    const auto result = match_defect_component(
        DefectComponentText{"1-1#", "主梁"},
        revision({entry("component-1", "1-1#", "主梁")}, "draft"), {});

    EXPECT_FALSE(result.matched_entry.has_value());
    EXPECT_EQ(result.candidate_component_ids, std::vector<std::string>{"component-1"});
}

TEST(ComponentMatcherTest, ChineseConfirmedInventoryAutoMatches) {
    const auto result = match_defect_component(
        {"1-1#", "主梁"},
        revision({entry("component-1", "1-1#", "主梁")}, "已确认"), {});

    ASSERT_TRUE(result.matched_entry.has_value());
    EXPECT_EQ(result.matched_entry->bridge_component_id, "component-1");
}

}  // namespace
