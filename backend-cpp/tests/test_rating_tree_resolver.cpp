#include <filesystem>

#include <gtest/gtest.h>

#include "bridge_report/rating_tree/RatingTreeCompiler.hpp"
#include "bridge_report/rating_tree/RatingTreeMatchText.hpp"
#include "bridge_report/rating_tree/RatingTreePackageLoader.hpp"
#include "bridge_report/rating_tree/RatingTreeResolver.hpp"
#include "bridge_report/standards/StandardPackageLoader.hpp"

namespace {

using bridge_report::rating_tree::EffectiveRatingTree;
using bridge_report::rating_tree::EffectiveRatingTreeNode;
using bridge_report::rating_tree::RatingTreeMatchInput;
using bridge_report::rating_tree::RatingTreeMatchOutcome;
using bridge_report::rating_tree::RatingTreeNodeType;
using bridge_report::rating_tree::RatingTreeResolver;
using bridge_report::rating_tree::RatingTreeScoringMode;
using bridge_report::rating_tree::RatingTreeSourceMapping;

constexpr const char* kBeam = "h21.bridge_type.beam";
constexpr const char* kUpper = "h21.component.beam.upper_general";
constexpr const char* kBearing = "h21.component.beam.upper_bearing";
constexpr const char* kWater = "org.bridge.water_damage";
constexpr const char* kSpalling = "org.bridge.spalling";

EffectiveRatingTreeNode make_node(
    std::string id,
    std::string display_name,
    std::vector<std::string> components,
    RatingTreeSourceMapping mapping) {
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
    mapping.target_node_id = node.id;
    node.source_mappings.push_back(std::move(mapping));
    return node;
}

EffectiveRatingTree tree_fixture() {
    EffectiveRatingTree tree;
    tree.version.tree_code = "organization-bridge";
    tree.version.package_version = "2.0.0";

    auto water = make_node(
        kWater,
        "水损",
        {kUpper},
        {"group-water", "index-water", "5.1.1", "5.1.1-13", ""});
    tree.nodes.emplace(water.id, water);
    auto spalling = make_node(
        kSpalling,
        "剥落、掉角",
        {kUpper},
        {"group-board", "index-spalling", "5.1.1", "5.1.1-2", ""});
    spalling.sort_order = 2;
    tree.nodes.emplace(spalling.id, spalling);
    auto crack = make_node(
        "org.bridge.crack",
        "裂缝",
        {kUpper, kBearing},
        {"group-board", "index-crack", "5.1.1", "5.1.1-1", ""});
    crack.sort_order = 3;
    tree.nodes.emplace(crack.id, crack);
    return tree;
}

RatingTreeMatchInput input_for(
    std::string group_id,
    std::string indicator_id,
    std::string group_number,
    std::string indicator_number,
    std::string component = kUpper) {
    RatingTreeMatchInput input;
    input.bridge_type_id = kBeam;
    input.component_category_id = std::move(component);
    input.source_defect_group_id = std::move(group_id);
    input.source_defect_indicator_id = std::move(indicator_id);
    input.source_defect_group_number = std::move(group_number);
    input.source_defect_indicator_number = std::move(indicator_number);
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

TEST(RatingTreeResolverTest, ResolvesByTheRawSourceIdPair) {
    auto input = input_for(
        "group-board", "index-spalling", "wrong-group", "wrong-index");
    input.defect_type = "水损";

    const auto result = RatingTreeResolver().resolve(tree_fixture(), input);

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::auto_bound);
    EXPECT_EQ(result.match_method, "source_indicator");
    ASSERT_TRUE(result.node_id.has_value());
    EXPECT_EQ(*result.node_id, kSpalling);
    EXPECT_NE(result.match_evidence.find("ID"), std::string::npos);
}

TEST(RatingTreeResolverTest, FallsBackToTheExactSourceNumberPair) {
    const auto result = RatingTreeResolver().resolve(
        tree_fixture(),
        input_for("unknown-group", "unknown-index", "5.1.1", "5.1.1-13"));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::auto_bound);
    ASSERT_TRUE(result.node_id.has_value());
    EXPECT_EQ(*result.node_id, kWater);
    EXPECT_NE(result.match_evidence.find("编号"), std::string::npos);
}

TEST(RatingTreeResolverTest, RequiresBothPartsOfTheSourceIdentity) {
    const auto result = RatingTreeResolver().resolve(
        tree_fixture(), input_for("", "index-spalling", "", "5.1.1-2"));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::unmatched);
    EXPECT_FALSE(result.node_id.has_value());
}

TEST(RatingTreeResolverTest, DoesNotUseDefectTextAliasesOrKeywords) {
    auto input = input_for("", "", "", "");
    input.defect_type = "水损";
    input.defect_description = "受渗水侵蚀，混凝土剥蚀破损";
    input.defect_location = "梁底裂缝";

    const auto result = RatingTreeResolver().resolve(tree_fixture(), input);

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::unmatched);
    EXPECT_TRUE(result.candidates.empty());
    EXPECT_EQ(result.reason_code, "no_matching_rule");
}

TEST(RatingTreeResolverTest, DoesNotMatchAnIndicatorIdWithoutItsGroupId) {
    const auto result = RatingTreeResolver().resolve(
        tree_fixture(), input_for("group-water", "index-spalling", "", ""));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::unmatched);
}

TEST(RatingTreeResolverTest, KeepsMappingsInsideTheComponentScope) {
    const auto result = RatingTreeResolver().resolve(
        tree_fixture(),
        input_for(
            "group-board", "index-spalling", "5.1.1", "5.1.1-2", kBearing));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::unmatched);
}

TEST(RatingTreeResolverTest, ReportsAnUnmappedComponentScope) {
    const auto result = RatingTreeResolver().resolve(
        tree_fixture(),
        input_for(
            "group-board",
            "index-spalling",
            "5.1.1",
            "5.1.1-2",
            "h21.component.deck.pavement"));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::prerequisite_missing);
    EXPECT_EQ(result.reason_code, "component_category_unmapped");
}

TEST(RatingTreeResolverTest, ReturnsCandidatesForAnInvalidDuplicateMapping) {
    auto tree = tree_fixture();
    tree.nodes.at(kWater).source_mappings.push_back({
        "group-board", "index-spalling", "8.8", "8.8-1", kWater});

    const auto result = RatingTreeResolver().resolve(
        tree, input_for("group-board", "index-spalling", "", ""));

    EXPECT_EQ(result.outcome, RatingTreeMatchOutcome::candidates);
    EXPECT_FALSE(result.node_id.has_value());
    EXPECT_EQ(result.candidates.size(), 2u);
    EXPECT_EQ(result.reason_code, "multiple_candidates");
}

TEST(RatingTreeResolverIntegrationTest, DistinguishesTheRealPierCapSourceMappings) {
    const auto root = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT);
    bridge_report::standards::StandardPackageLoader standard_loader;
    const auto h21 = standard_loader.load(
        root / "standards/technical-condition/jtg-t-h21-2011/1.0.4");
    const auto maintenance = standard_loader.load(
        root / "standards/maintenance/jtg-5120-2021/1.0.0");
    const auto extension =
        bridge_report::rating_tree::RatingTreePackageLoader().load(
            root / "standards/rating-tree/organization-bridge/2.0.3");
    ASSERT_TRUE(h21.ok());
    ASSERT_TRUE(maintenance.ok());
    ASSERT_TRUE(extension.ok());

    const auto compiled = bridge_report::rating_tree::RatingTreeCompiler().compile(
        *h21.package, &*maintenance.package, *extension.package);
    ASSERT_TRUE(compiled.ok())
        << (compiled.issues.empty() ? "" : compiled.issues.front().message);

    RatingTreeResolver resolver;
    const auto reused_indicator = resolver.resolve(
        *compiled.tree,
        input_for(
            "d493927d-e0a5-4fca-b21b-74a12ee68e72",
            "7cb25139-fd1f-48ed-9ee6-32ba5a7d9580",
            "9.1.2",
            "9.1.1-1",
            "h21.component.lower.pier"));
    ASSERT_EQ(reused_indicator.outcome, RatingTreeMatchOutcome::auto_bound);
    EXPECT_EQ(
        *reused_indicator.node_id,
        "org.bridge.defect.9_1_2__9_1_1_1");

    const auto original_group = resolver.resolve(
        *compiled.tree,
        input_for(
            "0d515ef0-3919-45a3-9459-6db1293d5d08",
            "7cb25139-fd1f-48ed-9ee6-32ba5a7d9580",
            "9.1.1",
            "9.1.1-1",
            "h21.component.lower.pier"));
    ASSERT_EQ(original_group.outcome, RatingTreeMatchOutcome::auto_bound);
    EXPECT_EQ(*original_group.node_id, "org.bridge.defect.9_1_1_1");

    const auto crack = resolver.resolve(
        *compiled.tree,
        input_for(
            "d493927d-e0a5-4fca-b21b-74a12ee68e72",
            "63cd142a-2363-4697-81f8-e425a4073949",
            "9.1.2",
            "9.1.2-1",
            "h21.component.lower.pier"));
    ASSERT_EQ(crack.outcome, RatingTreeMatchOutcome::auto_bound);
    EXPECT_EQ(*crack.node_id, "org.bridge.defect.9_1_2_1");
    EXPECT_EQ(crack.h21_indicator_id, "h21.defect.9_1_2");
    EXPECT_FALSE(crack.allowed_scales.empty());

    const auto other = resolver.resolve(
        *compiled.tree,
        input_for(
            "d493927d-e0a5-4fca-b21b-74a12ee68e72",
            "3ab76433-6786-492d-aacf-d3f655c5f592",
            "9.1.2",
            "9.1.2-2",
            "h21.component.lower.pier"));
    ASSERT_EQ(other.outcome, RatingTreeMatchOutcome::auto_bound);
    EXPECT_EQ(*other.node_id, "org.bridge.defect.9_1_2_2");
    EXPECT_FALSE(other.h21_indicator_id.has_value());
    EXPECT_TRUE(other.allowed_scales.empty());
}
