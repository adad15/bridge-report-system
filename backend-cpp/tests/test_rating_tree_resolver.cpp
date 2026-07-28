#include <gtest/gtest.h>

#include "bridge_report/rating_tree/RatingTreeResolver.hpp"

namespace {

bridge_report::rating_tree::EffectiveRatingTree tree_fixture() {
    using namespace bridge_report::rating_tree;
    EffectiveRatingTree tree;
    tree.version.tree_code = "organization-bridge";
    tree.version.package_version = "1.0.0";

    EffectiveRatingTreeNode water;
    water.id = "org.bridge.water_damage";
    water.display_name = "水损";
    water.node_type = RatingTreeNodeType::defect;
    water.scoring_mode = RatingTreeScoringMode::reference_h21;
    water.h21_indicator_id = "h21.defect.5_1_1_6";
    water.allowed_scales = {1, 2, 3, 4};
    water.is_selectable = true;
    water.bridge_type_ids = {"h21.bridge_type.beam"};
    water.component_category_ids = {"h21.component.beam.upper_general"};
    tree.nodes.emplace(water.id, water);

    tree.aliases.push_back(RatingTreeAlias{
        "渗水泛碱",
        water.id,
        "h21.bridge_type.beam",
        "h21.component.beam.upper_general"});
    return tree;
}

TEST(RatingTreeResolverTest, ResolvesAUniqueControlledAliasInsideItsScope) {
    bridge_report::rating_tree::RatingTreeResolver resolver;
    const auto tree = tree_fixture();

    const auto result = resolver.resolve(
        tree,
        "h21.bridge_type.beam",
        "h21.component.beam.upper_general",
        "渗水泛碱");

    EXPECT_EQ(
        result.status,
        bridge_report::rating_tree::RatingTreeResolutionStatus::resolved);
    EXPECT_EQ(result.match_method, "controlled_alias");
    ASSERT_TRUE(result.node_id.has_value());
    EXPECT_EQ(*result.node_id, "org.bridge.water_damage");
    ASSERT_TRUE(result.h21_indicator_id.has_value());
    EXPECT_EQ(*result.h21_indicator_id, "h21.defect.5_1_1_6");
    EXPECT_EQ(result.allowed_scales, (std::vector<int>{1, 2, 3, 4}));
}

TEST(RatingTreeResolverTest, SimilarTextOnlyReturnsCandidatesWithoutBinding) {
    bridge_report::rating_tree::RatingTreeResolver resolver;
    const auto tree = tree_fixture();

    const auto result = resolver.resolve(
        tree,
        "h21.bridge_type.beam",
        "h21.component.beam.upper_general",
        "混凝土水损");

    EXPECT_EQ(
        result.status,
        bridge_report::rating_tree::RatingTreeResolutionStatus::candidates);
    EXPECT_FALSE(result.node_id.has_value());
    EXPECT_EQ(result.match_method, "fuzzy_candidate");
    EXPECT_EQ(
        result.candidate_node_ids,
        (std::vector<std::string>{"org.bridge.water_damage"}));
}

TEST(RatingTreeResolverTest, FuzzyCandidatesNeverCrossBridgeOrComponentScope) {
    bridge_report::rating_tree::RatingTreeResolver resolver;
    const auto tree = tree_fixture();

    const auto result = resolver.resolve(
        tree,
        "h21.bridge_type.cable_stayed",
        "h21.component.cable_stayed.main_girder",
        "混凝土水损");

    EXPECT_EQ(
        result.status,
        bridge_report::rating_tree::RatingTreeResolutionStatus::not_found);
    EXPECT_TRUE(result.candidate_node_ids.empty());
}

TEST(RatingTreeResolverTest, DoesNotApplyAliasAcrossComponentScope) {
    bridge_report::rating_tree::RatingTreeResolver resolver;
    const auto tree = tree_fixture();

    const auto result = resolver.resolve(
        tree,
        "h21.bridge_type.beam",
        "h21.component.beam.upper_bearing",
        "渗水泛碱");

    EXPECT_EQ(
        result.status,
        bridge_report::rating_tree::RatingTreeResolutionStatus::not_found);
    EXPECT_FALSE(result.node_id.has_value());
}

TEST(RatingTreeResolverTest, ResolvesAnExactNameOnlyWhenUniqueInScope) {
    bridge_report::rating_tree::RatingTreeResolver resolver;
    const auto tree = tree_fixture();

    const auto result = resolver.resolve(
        tree,
        "h21.bridge_type.beam",
        "h21.component.beam.upper_general",
        "水损");

    EXPECT_EQ(
        result.status,
        bridge_report::rating_tree::RatingTreeResolutionStatus::resolved);
    EXPECT_EQ(result.match_method, "exact");
}

}  // namespace
