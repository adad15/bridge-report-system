#include <gtest/gtest.h>

#include "bridge_report/rating_tree/RatingTreeMatchText.hpp"
#include "bridge_report/rating_tree/RatingTreeResolver.hpp"

namespace {

using bridge_report::rating_tree::EffectiveRatingTree;
using bridge_report::rating_tree::EffectiveRatingTreeNode;
using bridge_report::rating_tree::RatingTreeAlias;
using bridge_report::rating_tree::RatingTreeKeywordRule;
using bridge_report::rating_tree::RatingTreeMatchInput;
using bridge_report::rating_tree::RatingTreeMatchOutcome;
using bridge_report::rating_tree::RatingTreeNodeType;
using bridge_report::rating_tree::RatingTreeResolver;
using bridge_report::rating_tree::RatingTreeScoringMode;

constexpr const char* kBeam = "h21.bridge_type.beam";
constexpr const char* kUpper = "h21.component.beam.upper_general";
constexpr const char* kBearing = "h21.component.beam.upper_bearing";
constexpr const char* kWater = "org.bridge.water_damage";
constexpr const char* kSpalling = "org.bridge.spalling";

EffectiveRatingTreeNode make_node(
    std::string id,
    std::string display_name,
    std::vector<std::string> components) {
    EffectiveRatingTreeNode node;
    node.id = std::move(id);
    node.display_name = std::move(display_name);
    node.node_type = RatingTreeNodeType::defect;
    node.scoring_mode = RatingTreeScoringMode::reference_h21;
    node.h21_indicator_id = "h21.defect.5_1_1_6";
    node.allowed_scales = {1, 2, 3, 4};
    node.is_selectable = true;
    node.is_scoring = true;
    node.bridge_type_ids = {kBeam};
    node.component_category_ids = std::move(components);
    return node;
}

RatingTreeKeywordRule make_rule(
    std::string rule_id,
    std::string target,
    std::vector<std::string> positive,
    std::vector<std::string> excluded,
    const bool auto_bind,
    const int sort_order) {
    RatingTreeKeywordRule rule;
    rule.rule_id = std::move(rule_id);
    rule.target_node_id = std::move(target);
    rule.bridge_type_id = kBeam;
    rule.component_category_id = kUpper;
    rule.positive_keywords = std::move(positive);
    rule.excluded_keywords = std::move(excluded);
    rule.auto_bind = auto_bind;
    rule.sort_order = sort_order;
    return rule;
}

EffectiveRatingTree tree_fixture() {
    EffectiveRatingTree tree;
    tree.version.tree_code = "organization-bridge";
    tree.version.package_version = "1.0.3";

    auto water = make_node(kWater, "水损", {kUpper});
    tree.nodes.emplace(water.id, water);
    auto spalling = make_node(kSpalling, "剥落、掉角", {kUpper});
    spalling.sort_order = 2;
    tree.nodes.emplace(spalling.id, spalling);
    auto crack = make_node("org.bridge.crack", "裂缝", {kUpper, kBearing});
    crack.sort_order = 3;
    tree.nodes.emplace(crack.id, crack);

    for (const auto& alias : {"渗水泛碱", "泛碱", "受渗水侵蚀"}) {
        tree.aliases.push_back(RatingTreeAlias{alias, kWater, kBeam, kUpper});
    }
    tree.keyword_rules.push_back(make_rule(
        "rule.water", kWater, {"渗水"}, {"泄水管", "渗水孔"}, true, 10));
    tree.keyword_rules.push_back(
        make_rule("rule.spalling", kSpalling, {"剥蚀"}, {}, false, 20));
    return tree;
}

RatingTreeMatchInput input_for(
    std::string defect_type,
    std::string description = "",
    std::string component = kUpper) {
    RatingTreeMatchInput input;
    input.bridge_type_id = kBeam;
    input.component_category_id = std::move(component);
    input.defect_type = std::move(defect_type);
    input.defect_description = std::move(description);
    return input;
}

}  // namespace

TEST(RatingTreeMatchTextTest, KeepsMeaningfulSlashesAndDigitsInsideTheText) {
    using bridge_report::rating_tree::normalize_match_key;
    EXPECT_EQ(normalize_match_key("  L/W=2 "), "l/w=2");
    EXPECT_EQ(normalize_match_key("板底/腹板交界处"), "板底/腹板交界处");
    EXPECT_EQ(normalize_match_key("1-1#板"), "1-1#板");
}

TEST(RatingTreeMatchTextTest, StripsOnlyLeadingAndTrailingSeparators) {
    using bridge_report::rating_tree::normalize_match_key;
    EXPECT_EQ(normalize_match_key("/渗水泛碱/"), "渗水泛碱");
    EXPECT_EQ(normalize_match_key("／渗水泛碱"), "渗水泛碱");
    EXPECT_EQ(normalize_match_key("—"), "");
}

TEST(RatingTreeMatchTextTest, UnifiesFullWidthPunctuationAndCollapsesSpaces) {
    using bridge_report::rating_tree::normalize_match_key;
    EXPECT_EQ(normalize_match_key("水损（梁底）"), "水损(梁底)");
    EXPECT_EQ(normalize_match_key("梁底　　渗水"), "梁底 渗水");
}

TEST(RatingTreeMatchTextTest, SplitsSegmentsOnUnifiedPunctuation) {
    const auto segments =
        bridge_report::rating_tree::split_match_segments("受渗水侵蚀，混凝土剥蚀破损");
    ASSERT_EQ(segments.size(), 2u);
    EXPECT_EQ(segments[0], "受渗水侵蚀");
    EXPECT_EQ(segments[1], "混凝土剥蚀破损");
}

TEST(RatingTreeResolverTest, ResolvesAnExactSpecificationNameInsideScope) {
    const auto result = RatingTreeResolver().resolve(tree_fixture(), input_for("水损"));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::auto_bound);
    EXPECT_EQ(result.match_method, "exact");
    ASSERT_TRUE(result.node_id.has_value());
    EXPECT_EQ(*result.node_id, kWater);
    EXPECT_EQ(result.allowed_scales, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_NE(result.match_evidence.find("构件适用依据"), std::string::npos);
}

TEST(RatingTreeResolverTest, ResolvesAUniqueControlledAliasInsideItsScope) {
    const auto result =
        RatingTreeResolver().resolve(tree_fixture(), input_for("渗水泛碱"));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::auto_bound);
    EXPECT_EQ(result.match_method, "controlled_alias");
    ASSERT_TRUE(result.node_id.has_value());
    EXPECT_EQ(*result.node_id, kWater);
    ASSERT_TRUE(result.h21_indicator_id.has_value());
    EXPECT_EQ(*result.h21_indicator_id, "h21.defect.5_1_1_6");
}

TEST(RatingTreeResolverTest, DoesNotApplyAliasAcrossComponentScope) {
    const auto result = RatingTreeResolver().resolve(
        tree_fixture(), input_for("渗水泛碱", "", kBearing));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::unmatched);
    EXPECT_FALSE(result.node_id.has_value());
    EXPECT_EQ(result.reason_code, "no_matching_rule");
}

TEST(RatingTreeResolverTest, FallsBackToAControlledKeywordWhenTheTypeCellIsEmpty) {
    const auto result =
        RatingTreeResolver().resolve(tree_fixture(), input_for("", "板底存在渗水泛碱"));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::auto_bound);
    EXPECT_EQ(result.match_method, "controlled_keyword");
    ASSERT_TRUE(result.node_id.has_value());
    EXPECT_EQ(*result.node_id, kWater);
}

TEST(RatingTreeResolverTest, ExcludedKeywordsBlockTheAutomaticBinding) {
    const auto result = RatingTreeResolver().resolve(
        tree_fixture(), input_for("", "泄水管周边渗水"));

    EXPECT_NE(result.outcome, RatingTreeMatchOutcome::auto_bound);
    EXPECT_FALSE(result.node_id.has_value());
    EXPECT_EQ(result.reason_code, "no_matching_rule");
}

TEST(RatingTreeResolverTest, RecommendOnlyRulesNeverBindAutomatically) {
    const auto result =
        RatingTreeResolver().resolve(tree_fixture(), input_for("", "梁底混凝土剥蚀"));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::candidates);
    EXPECT_FALSE(result.node_id.has_value());
    ASSERT_EQ(result.candidates.size(), 1u);
    EXPECT_EQ(result.candidates[0].node_id, kSpalling);
    EXPECT_EQ(result.reason_code, "candidate_requires_review");
}

TEST(RatingTreeResolverTest, TwoDistinctHitsAreReportedAsACompositeDefect) {
    const auto result = RatingTreeResolver().resolve(
        tree_fixture(), input_for("受渗水侵蚀，混凝土剥蚀破损"));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::composite);
    EXPECT_FALSE(result.node_id.has_value());
    EXPECT_EQ(result.reason_code, "composite_defect");
    ASSERT_EQ(result.candidates.size(), 2u);
    EXPECT_EQ(result.candidates[0].node_id, kWater);
    EXPECT_EQ(result.candidates[1].node_id, kSpalling);
}

TEST(RatingTreeResolverTest, SimilarTextOnlyReturnsCandidatesWithoutBinding) {
    const auto result =
        RatingTreeResolver().resolve(tree_fixture(), input_for("混凝土水损裂缝"));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::candidates);
    EXPECT_FALSE(result.node_id.has_value());
    EXPECT_EQ(result.match_method, "fuzzy_candidate");
    EXPECT_EQ(result.reason_code, "multiple_candidates");
    EXPECT_LE(result.candidates.size(), 3u);
}

TEST(RatingTreeResolverTest, CandidateListsNeverGrowBeyondThreeEntries) {
    auto tree = tree_fixture();
    for (int index = 0; index < 5; ++index) {
        auto node = make_node(
            "org.bridge.extra." + std::to_string(index),
            "长裂缝" + std::to_string(index),
            {kUpper});
        node.sort_order = 10 + index;
        tree.nodes.emplace(node.id, node);
        tree.keyword_rules.push_back(make_rule(
            "rule.extra." + std::to_string(index),
            node.id,
            {"复合"},
            {},
            false,
            30 + index));
    }

    const auto result = RatingTreeResolver().resolve(tree, input_for("", "复合病害"));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::composite);
    EXPECT_EQ(result.candidates.size(), 3u);
}

TEST(RatingTreeResolverTest, ReportsAnUnmappedScopeInsteadOfAPlainMiss) {
    const auto result = RatingTreeResolver().resolve(
        tree_fixture(),
        input_for("水损", "", "h21.component.deck.pavement"));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::prerequisite_missing);
    EXPECT_EQ(result.reason_code, "component_category_unmapped");
}

TEST(RatingTreeResolverTest, ProducesTheSameResultForRepeatedRuns) {
    const auto tree = tree_fixture();
    const auto first =
        RatingTreeResolver().resolve(tree, input_for("受渗水侵蚀，混凝土剥蚀破损"));
    const auto second =
        RatingTreeResolver().resolve(tree, input_for("受渗水侵蚀，混凝土剥蚀破损"));

    ASSERT_EQ(first.candidates.size(), second.candidates.size());
    for (std::size_t index = 0; index < first.candidates.size(); ++index) {
        EXPECT_EQ(first.candidates[index].node_id, second.candidates[index].node_id);
        EXPECT_EQ(first.candidates[index].evidence, second.candidates[index].evidence);
    }
    EXPECT_EQ(first.reason_code, second.reason_code);
}
