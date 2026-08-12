#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/review/DefectRatingTreeMatching.hpp"

namespace {

using bridge_report::rating_tree::EffectiveRatingTree;
using bridge_report::rating_tree::EffectiveRatingTreeNode;
using bridge_report::rating_tree::RatingTreeAlias;
using bridge_report::rating_tree::RatingTreeKeywordRule;
using bridge_report::rating_tree::RatingTreeMatchOutcome;
using bridge_report::rating_tree::RatingTreeNodeType;
using bridge_report::rating_tree::RatingTreeScoringMode;
using bridge_report::review::DefectMatchScope;
using bridge_report::review::match_defect_rating_tree_nodes;

constexpr const char* kBeam = "beam";
constexpr const char* kUpper = "upper_general";
constexpr const char* kWaterNode = "11111111-1111-4111-8111-111111111111";
constexpr const char* kSpallingNode = "22222222-2222-4222-8222-222222222222";
constexpr const char* kPackage = "h21-package";
constexpr const char* kTreeVersion = "tree-version-1";

EffectiveRatingTree tree_fixture() {
    EffectiveRatingTree tree;
    EffectiveRatingTreeNode water;
    water.id = kWaterNode;
    water.display_name = "水损";
    water.node_type = RatingTreeNodeType::defect;
    water.scoring_mode = RatingTreeScoringMode::reference_h21;
    water.h21_indicator_id = "h21.defect.5_1_1_6";
    water.allowed_scales = {1, 2, 3, 4};
    water.is_selectable = true;
    water.is_scoring = true;
    water.bridge_type_ids = {kBeam};
    water.component_category_ids = {kUpper};
    water.source_mappings.push_back({
        "group-water", "index-water", "5.1.1", "5.1.1-13", kWaterNode});
    tree.nodes.emplace(water.id, water);

    EffectiveRatingTreeNode spalling = water;
    spalling.id = kSpallingNode;
    spalling.display_name = "剥落、掉角";
    spalling.sort_order = 2;
    spalling.source_mappings = {{
        "group-board", "index-spalling", "5.1.1", "5.1.1-2", kSpallingNode}};
    tree.nodes.emplace(spalling.id, spalling);

    tree.aliases.push_back(RatingTreeAlias{"受渗水侵蚀", kWaterNode, kBeam, kUpper});
    RatingTreeKeywordRule water_rule;
    water_rule.rule_id = "rule.water";
    water_rule.target_node_id = kWaterNode;
    water_rule.bridge_type_id = kBeam;
    water_rule.component_category_id = kUpper;
    water_rule.positive_keywords = {"渗水"};
    water_rule.auto_bind = true;
    water_rule.sort_order = 10;
    tree.keyword_rules.push_back(std::move(water_rule));

    RatingTreeKeywordRule spalling_rule;
    spalling_rule.rule_id = "rule.spalling";
    spalling_rule.target_node_id = kSpallingNode;
    spalling_rule.bridge_type_id = kBeam;
    spalling_rule.component_category_id = kUpper;
    spalling_rule.positive_keywords = {"剥蚀"};
    spalling_rule.auto_bind = false;
    spalling_rule.sort_order = 20;
    tree.keyword_rules.push_back(std::move(spalling_rule));
    return tree;
}

bridge_report::inventory::InventoryRevision inventory_fixture() {
    bridge_report::inventory::InventoryRevision revision;
    revision.id = "revision-1";
    revision.status = "已确认";
    bridge_report::inventory::InventoryEntry entry;
    entry.bridge_component_id = "component-1";
    entry.is_active = true;
    bridge_report::inventory::InventoryMapping mapping;
    mapping.standard_package_id = kPackage;
    mapping.standard_bridge_type_id = kBeam;
    mapping.standard_component_category_id = kUpper;
    mapping.confirmation_status = "已确认";
    mapping.is_active = true;
    entry.mappings.push_back(std::move(mapping));
    revision.entries.push_back(std::move(entry));
    return revision;
}

Json::Value make_defect(
    const std::string& candidate_id,
    const std::string& defect_type,
    const std::string& description = "",
    const bool bound = true) {
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = candidate_id;
    defect["defect_type"] = defect_type;
    defect["defect_location"] = "";
    defect["defect_description"] = description.empty() ? defect_type : description;
    defect["review_status"] = "待确认";
    defect["group_review_status"] = "待确认";
    if (bound) defect["bridge_component_id"] = "component-1";
    return defect;
}

Json::Value make_draft(const std::vector<Json::Value>& defects) {
    Json::Value draft(Json::objectValue);
    draft["defects"] = Json::Value(Json::arrayValue);
    for (const auto& defect : defects) draft["defects"].append(defect);
    return draft;
}

Json::Value with_source(
    Json::Value defect,
    const std::string& group_id,
    const std::string& indicator_id,
    const std::string& group_number,
    const std::string& indicator_number) {
    defect["source_defect_group_id"] = group_id;
    defect["source_defect_indicator_id"] = indicator_id;
    defect["source_defect_group_number"] = group_number;
    defect["source_defect_indicator_number"] = indicator_number;
    return defect;
}

bridge_report::review::DefectMatchReport run(Json::Value& draft, const bool apply = true) {
    return match_defect_rating_tree_nodes(
        draft, kTreeVersion, kPackage, tree_fixture(), inventory_fixture(),
        DefectMatchScope{}, apply);
}

}  // namespace

TEST(DefectRatingTreeMatchingTest, BindsUniqueResultsWithoutConfirmingThem) {
    auto draft = make_draft({with_source(
        make_defect("d1", "文字与映射无关"),
        "group-water",
        "index-water",
        "5.1.1",
        "5.1.1-13")});

    const auto report = run(draft);

    EXPECT_EQ(report.stats.processed, 1);
    EXPECT_EQ(report.stats.auto_bound, 1);
    const auto& defect = draft["defects"][0];
    EXPECT_EQ(defect["rating_tree_node_id"].asString(), kWaterNode);
    EXPECT_EQ(defect["rating_tree_match_method"].asString(), "source_indicator");
    EXPECT_EQ(defect["standard_defect_indicator_id"].asString(), "h21.defect.5_1_1_6");
    // 自动绑定不等于确认：记录仍然停留在待确认。
    EXPECT_EQ(defect["review_status"].asString(), "待确认");
    EXPECT_EQ(defect["group_review_status"].asString(), "待确认");
}

TEST(DefectRatingTreeMatchingTest, TextDoesNotCreateAnAutomaticBinding) {
    auto draft = make_draft({make_defect("d1", "", "板底存在渗水泛碱")});

    const auto report = run(draft);

    EXPECT_EQ(report.stats.auto_bound, 0);
    EXPECT_EQ(report.stats.unmatched, 1);
    EXPECT_TRUE(draft["defects"][0]["rating_tree_node_id"].isNull());
}

TEST(DefectRatingTreeMatchingTest, CompositeTextDoesNotCreateCandidates) {
    auto draft =
        make_draft({make_defect("d1", "受渗水侵蚀，混凝土剥蚀破损")});

    const auto report = run(draft);

    EXPECT_EQ(report.stats.unmatched, 1);
    EXPECT_EQ(report.stats.auto_bound, 0);
    EXPECT_TRUE(draft["defects"][0]["rating_tree_node_id"].isNull());
    ASSERT_EQ(report.records.size(), 1U);
    EXPECT_EQ(report.records[0].outcome, RatingTreeMatchOutcome::unmatched);
    EXPECT_EQ(report.records[0].reason_code, "no_matching_rule");
    EXPECT_TRUE(report.records[0].candidates.empty());
}

TEST(DefectRatingTreeMatchingTest, SeparatesMissingPrerequisitesFromRealMisses) {
    auto draft = make_draft({
        make_defect("d1", "水损", "", false),
        make_defect("d2", "无此规范病害"),
    });

    const auto report = run(draft);

    EXPECT_EQ(report.stats.prerequisite_missing, 1);
    EXPECT_EQ(report.stats.unmatched, 1);
    ASSERT_EQ(report.records.size(), 2U);
    EXPECT_EQ(report.records[0].reason_code, "component_not_bound");
    EXPECT_EQ(report.records[1].reason_code, "no_matching_rule");
}

TEST(DefectRatingTreeMatchingTest, NeverOverwritesManualSelections) {
    auto defect = make_defect("d1", "水损");
    defect["rating_tree_node_id"] = kSpallingNode;
    defect["rating_tree_match_method"] = "manual";
    defect["rating_tree_match_evidence"] = "用户选择";
    auto draft = make_draft({defect});

    const auto report = run(draft);

    EXPECT_EQ(report.stats.skipped, 1);
    EXPECT_EQ(report.stats.auto_bound, 0);
    EXPECT_EQ(draft["defects"][0]["rating_tree_node_id"].asString(), kSpallingNode);
    EXPECT_EQ(draft["defects"][0]["rating_tree_match_method"].asString(), "manual");
    EXPECT_TRUE(report.records[0].skipped);
}

TEST(DefectRatingTreeMatchingTest, NeverOverwritesConfirmedOrIgnoredRecords) {
    auto confirmed = make_defect("d1", "水损");
    confirmed["group_review_status"] = "已确认";
    confirmed["rating_tree_node_id"] = kSpallingNode;
    auto ignored = make_defect("d2", "水损");
    ignored["review_status"] = "已忽略";
    auto draft = make_draft({confirmed, ignored});

    const auto report = run(draft);

    EXPECT_EQ(report.stats.skipped, 2);
    EXPECT_EQ(draft["defects"][0]["rating_tree_node_id"].asString(), kSpallingNode);
    EXPECT_TRUE(draft["defects"][1]["rating_tree_node_id"].isNull());
}

TEST(DefectRatingTreeMatchingTest, PreviewModeLeavesTheDraftUntouched) {
    auto draft = make_draft({with_source(
        make_defect("d1", "水损"),
        "group-water",
        "index-water",
        "5.1.1",
        "5.1.1-13")});
    const auto before = draft;

    const auto report = run(draft, false);

    EXPECT_EQ(report.stats.auto_bound, 1);
    EXPECT_EQ(draft, before);
    ASSERT_EQ(report.records.size(), 1U);
    ASSERT_TRUE(report.records[0].node_id.has_value());
    EXPECT_EQ(*report.records[0].node_id, kWaterNode);
}

TEST(DefectRatingTreeMatchingTest, RepeatedRunsAreIdempotentAndKeepTheDefectCount) {
    auto draft = make_draft({
        make_defect("d1", "水损"),
        make_defect("d2", "受渗水侵蚀，混凝土剥蚀破损"),
        make_defect("d3", "无此规范病害"),
    });

    const auto first = run(draft);
    const auto after_first = draft;
    const auto second = run(draft);

    EXPECT_EQ(draft, after_first);
    EXPECT_EQ(draft["defects"].size(), 3U);
    EXPECT_EQ(first.stats.auto_bound, second.stats.auto_bound);
    EXPECT_EQ(first.stats.composite, second.stats.composite);
    EXPECT_EQ(first.stats.unmatched, second.stats.unmatched);
}

TEST(DefectRatingTreeMatchingTest, ScopeLimitsProcessingToTheSelectedCandidates) {
    auto first = with_source(
        make_defect("d1", "水损"),
        "group-water",
        "index-water",
        "5.1.1",
        "5.1.1-13");
    auto second = with_source(
        make_defect("d2", "水损"),
        "group-water",
        "index-water",
        "5.1.1",
        "5.1.1-13");
    auto draft = make_draft({first, second});
    DefectMatchScope scope;
    scope.has_scope = true;
    scope.candidate_ids.insert("d2");

    const auto report = match_defect_rating_tree_nodes(
        draft, kTreeVersion, kPackage, tree_fixture(), inventory_fixture(), scope,
        true);

    EXPECT_EQ(report.stats.processed, 1);
    EXPECT_TRUE(draft["defects"][0]["rating_tree_node_id"].isNull());
    EXPECT_EQ(draft["defects"][1]["rating_tree_node_id"].asString(), kWaterNode);
}
