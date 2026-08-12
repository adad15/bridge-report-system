#include <filesystem>

#include <gtest/gtest.h>

#include "bridge_report/rating_tree/RatingTreeCompiler.hpp"
#include "bridge_report/rating_tree/RatingTreePackageLoader.hpp"
#include "bridge_report/standards/StandardPackageLoader.hpp"

namespace {

bridge_report::standards::StandardPackage minimal_h21_package() {
    bridge_report::standards::StandardPackage package;
    package.manifest.family =
        bridge_report::standards::StandardFamily::technical_condition;
    package.manifest.standard_id = "jtg-t-h21-2011";
    package.manifest.package_version = "1.0.1";
    package.manifest.content_checksum = "sha256:h21";

    Json::Value catalog;
    catalog["id"] = "h21.defect_catalog.test";
    catalog["applicable_component_ids"] = Json::Value(Json::arrayValue);
    catalog["applicable_component_ids"].append("h21.component.beam.upper_general");
    catalog["indicators"] = Json::Value(Json::arrayValue);
    Json::Value indicator;
    indicator["id"] = "h21.defect.5_1_1_6";
    indicator["name"] = "混凝土碳化";
    indicator["allowed_scales"] = Json::Value(Json::arrayValue);
    indicator["scale_descriptions"] = Json::Value(Json::objectValue);
    for (int scale = 1; scale <= 4; ++scale) {
        indicator["allowed_scales"].append(scale);
        indicator["scale_descriptions"][std::to_string(scale)] =
            "标度" + std::to_string(scale);
    }
    indicator["deduction_rule_id"] = "h21.deduction.scale_table.max_4";
    indicator["source_table"] = "5.1.1-6";
    catalog["indicators"].append(indicator);
    package.definitions.emplace(
        "h21.defect_catalog.test",
        bridge_report::standards::StandardDefinition{
            "h21.defect_catalog.test", {}, "defect-indicators.json", catalog});

    Json::Value deduction;
    deduction["id"] = "h21.deduction.scale_table.max_4";
    deduction["points"] = Json::Value(Json::objectValue);
    deduction["points"]["1"] = 0;
    deduction["points"]["2"] = 25;
    deduction["points"]["3"] = 40;
    deduction["points"]["4"] = 50;
    package.definitions.emplace(
        "h21.deduction.scale_table.max_4",
        bridge_report::standards::StandardDefinition{
            "h21.deduction.scale_table.max_4", {}, "deduction-rules.json", deduction});
    return package;
}

bridge_report::rating_tree::RatingTreeExtensionPackage minimal_extension() {
    using namespace bridge_report::rating_tree;
    RatingTreeExtensionPackage extension;
    extension.manifest.tree_code = "organization-bridge";
    extension.manifest.tree_name = "单位桥梁评定树";
    extension.manifest.package_version = "1.0.0";
    extension.manifest.content_checksum = "sha256:organization";

    RatingTreeExtensionNode root;
    root.id = "org.bridge";
    root.display_name = "桥梁";
    root.node_type = RatingTreeNodeType::root;
    extension.nodes.emplace(root.id, root);

    RatingTreeExtensionNode water;
    water.id = "org.bridge.water_damage";
    water.parent_id = root.id;
    water.display_name = "水损";
    water.node_type = RatingTreeNodeType::defect;
    water.scoring_mode = RatingTreeScoringMode::reference_h21;
    water.h21_indicator_id = "h21.defect.5_1_1_6";
    water.is_selectable = true;
    water.bridge_type_ids = {"h21.bridge_type.beam"};
    water.component_category_ids = {"h21.component.beam.upper_general"};
    extension.nodes.emplace(water.id, water);
    return extension;
}

TEST(RatingTreeCompilerTest, ResolvesOrganizationNodeToH21Rules) {
    bridge_report::rating_tree::RatingTreeCompiler compiler;

    const auto result = compiler.compile(
        minimal_h21_package(), nullptr, minimal_extension());

    ASSERT_TRUE(result.ok());
    const auto& node = result.tree->nodes.at("org.bridge.water_damage");
    ASSERT_TRUE(node.h21_indicator_id.has_value());
    EXPECT_EQ(*node.h21_indicator_id, "h21.defect.5_1_1_6");
    EXPECT_EQ(node.allowed_scales, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_EQ(node.deduction_points.at(4), 50);
}

TEST(RatingTreeCompilerTest, UsesSourceScaleTextWithH21DeductionPoints) {
    auto extension = minimal_extension();
    auto& source = extension.nodes.at("org.bridge.water_damage");
    source.source_scale_descriptions = {
        {1, "少量"},
        {2, "局部渗水泛碱"},
        {3, "水蚀严重"},
        {4, "—"},
    };

    const auto result = bridge_report::rating_tree::RatingTreeCompiler().compile(
        minimal_h21_package(), nullptr, extension);

    ASSERT_TRUE(result.ok());
    const auto& node = result.tree->nodes.at("org.bridge.water_damage");
    EXPECT_TRUE(node.uses_source_scale_descriptions);
    EXPECT_EQ(node.scale_descriptions.at(2), "局部渗水泛碱");
    EXPECT_EQ(node.deduction_points.at(2), 25);
    EXPECT_EQ(node.deduction_points.at(4), 50);
}

TEST(RatingTreeCompilerTest, RejectsIncompleteSourceScaleText) {
    auto extension = minimal_extension();
    extension.nodes.at("org.bridge.water_damage").source_scale_descriptions = {
        {1, "少量"},
        {2, "局部渗水泛碱"},
        {3, "水蚀严重"},
    };

    const auto result = bridge_report::rating_tree::RatingTreeCompiler().compile(
        minimal_h21_package(), nullptr, extension);

    ASSERT_FALSE(result.ok());
    ASSERT_FALSE(result.issues.empty());
    EXPECT_EQ(result.issues.front().code, "rating_tree_source_scale_invalid");
}

TEST(RatingTreeCompilerTest, PreservesDisplayNumberAndSourceMapping) {
    auto extension = minimal_extension();
    auto& source = extension.nodes.at("org.bridge.water_damage");
    source.display_number = "5.1.1-6";
    extension.source_mappings.push_back({
        "source-group-1",
        "source-indicator-1",
        "5.1.1",
        "5.1.1-6",
        source.id,
    });
    bridge_report::rating_tree::RatingTreeCompiler compiler;

    const auto result = compiler.compile(
        minimal_h21_package(), nullptr, extension);

    ASSERT_TRUE(result.ok());
    const auto& node = result.tree->nodes.at("org.bridge.water_damage");
    ASSERT_TRUE(node.display_number.has_value());
    EXPECT_EQ(*node.display_number, "5.1.1-6");
    ASSERT_EQ(node.source_mappings.size(), 1u);
    EXPECT_EQ(node.source_mappings[0].source_group_id, "source-group-1");
    EXPECT_EQ(node.source_mappings[0].source_indicator_id, "source-indicator-1");
    EXPECT_EQ(node.source_mappings[0].source_group_number, "5.1.1");
    EXPECT_EQ(node.source_mappings[0].source_indicator_number, "5.1.1-6");
}

TEST(RatingTreeCompilerTest, ContentChecksumCoversDisplayNumberAndSourceMapping) {
    bridge_report::rating_tree::RatingTreeCompiler compiler;
    const auto base = compiler.compile(
        minimal_h21_package(), nullptr, minimal_extension());
    ASSERT_TRUE(base.ok());

    auto numbered = minimal_extension();
    numbered.nodes.at("org.bridge.water_damage").display_number = "5.1.1-6";
    const auto numbered_result = compiler.compile(
        minimal_h21_package(), nullptr, numbered);
    ASSERT_TRUE(numbered_result.ok());
    EXPECT_NE(
        base.tree->version.tree_content_checksum,
        numbered_result.tree->version.tree_content_checksum);

    auto mapped = minimal_extension();
    mapped.source_mappings.push_back({
        "source-group-1",
        "source-indicator-1",
        "5.1.1",
        "5.1.1-6",
        "org.bridge.water_damage",
    });
    const auto mapped_result = compiler.compile(
        minimal_h21_package(), nullptr, mapped);
    ASSERT_TRUE(mapped_result.ok());
    EXPECT_NE(
        base.tree->version.tree_content_checksum,
        mapped_result.tree->version.tree_content_checksum);
}

TEST(RatingTreeCompilerTest, RejectsInvalidSourceMappingTarget) {
    auto extension = minimal_extension();
    extension.source_mappings.push_back({
        "source-group-1",
        "source-indicator-1",
        "5.1.1",
        "5.1.1-6",
        "org.bridge",
    });
    bridge_report::rating_tree::RatingTreeCompiler compiler;

    const auto result = compiler.compile(
        minimal_h21_package(), nullptr, extension);

    ASSERT_FALSE(result.ok());
    ASSERT_FALSE(result.issues.empty());
    EXPECT_EQ(
        result.issues.front().code,
        "rating_tree_source_mapping_target_invalid");
}

TEST(RatingTreeCompilerTest, RejectsNonScoringNodeWithH21Reference) {
    auto extension = minimal_extension();
    auto& source = extension.nodes.at("org.bridge.water_damage");
    source.scoring_mode =
        bridge_report::rating_tree::RatingTreeScoringMode::non_scoring;
    bridge_report::rating_tree::RatingTreeCompiler compiler;

    const auto result = compiler.compile(
        minimal_h21_package(), nullptr, extension);

    ASSERT_FALSE(result.ok());
    ASSERT_FALSE(result.issues.empty());
    EXPECT_EQ(
        result.issues.front().code,
        "rating_tree_non_scoring_target_invalid");
}

TEST(RatingTreeCompilerTest, RejectsScoringNodeWithoutValidH21Reference) {
    auto extension = minimal_extension();
    extension.nodes.at("org.bridge.water_damage").h21_indicator_id = "missing";
    bridge_report::rating_tree::RatingTreeCompiler compiler;

    const auto result = compiler.compile(minimal_h21_package(), nullptr, extension);

    ASSERT_FALSE(result.ok());
    ASSERT_FALSE(result.issues.empty());
    EXPECT_EQ(result.issues.front().code, "rating_tree_h21_indicator_missing");
}

TEST(RatingTreeCompilerTest, AllowsAnExplicitH21ReferenceAcrossComponentScopes) {
    auto extension = minimal_extension();
    auto& node = extension.nodes.at("org.bridge.water_damage");
    node.component_category_ids = {"organization.component.reused_context"};
    node.scoring_mode =
        bridge_report::rating_tree::RatingTreeScoringMode::reference_h21;
    bridge_report::rating_tree::RatingTreeCompiler compiler;

    const auto result = compiler.compile(
        minimal_h21_package(), nullptr, extension);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(
        result.tree->nodes.at("org.bridge.water_damage").h21_indicator_id,
        node.h21_indicator_id);
}

TEST(RatingTreeCompilerTest, RejectsInheritedH21OutsideItsComponentScope) {
    auto extension = minimal_extension();
    auto& node = extension.nodes.at("org.bridge.water_damage");
    node.component_category_ids = {"organization.component.wrong"};
    node.scoring_mode =
        bridge_report::rating_tree::RatingTreeScoringMode::inherit_h21;
    bridge_report::rating_tree::RatingTreeCompiler compiler;

    const auto result = compiler.compile(
        minimal_h21_package(), nullptr, extension);

    ASSERT_FALSE(result.ok());
    ASSERT_FALSE(result.issues.empty());
    EXPECT_EQ(
        result.issues.front().code,
        "rating_tree_h21_indicator_not_applicable");
}

TEST(RatingTreeCompilerTest, RejectsANonMaintenanceSecondaryPackage) {
    auto secondary = minimal_h21_package();
    bridge_report::rating_tree::RatingTreeCompiler compiler;

    const auto result = compiler.compile(
        minimal_h21_package(), &secondary, minimal_extension());

    ASSERT_FALSE(result.ok());
    ASSERT_FALSE(result.issues.empty());
    EXPECT_EQ(
        result.issues.front().code,
        "rating_tree_maintenance_package_invalid");
}

TEST(RatingTreeCompilerTest, ContentChecksumCoversRulesSourcesAndAliases) {
    bridge_report::rating_tree::RatingTreeCompiler compiler;
    const auto base = compiler.compile(
        minimal_h21_package(), nullptr, minimal_extension());
    ASSERT_TRUE(base.ok());

    auto changed_description_h21 = minimal_h21_package();
    auto& catalog = changed_description_h21.definitions
        .at("h21.defect_catalog.test")
        .payload;
    catalog["indicators"][0]["scale_descriptions"]["2"] = "修改后的判定文字";
    const auto description_result = compiler.compile(
        changed_description_h21, nullptr, minimal_extension());
    ASSERT_TRUE(description_result.ok());
    EXPECT_NE(
        base.tree->version.tree_content_checksum,
        description_result.tree->version.tree_content_checksum);

    auto changed_extension = minimal_extension();
    changed_extension.nodes.at("org.bridge.water_damage").organization_note =
        "修改后的单位说明";
    changed_extension.aliases.push_back({
        "泛碱水损",
        "org.bridge.water_damage",
        "h21.bridge_type.beam",
        "h21.component.beam.upper_general"});
    const auto extension_result = compiler.compile(
        minimal_h21_package(), nullptr, changed_extension);
    ASSERT_TRUE(extension_result.ok());
    EXPECT_NE(
        base.tree->version.tree_content_checksum,
        extension_result.tree->version.tree_content_checksum);
}

TEST(RatingTreeCompilerIntegrationTest, CompilesThePublishedOrganizationBridgeTree) {
    const auto root = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT);
    bridge_report::standards::StandardPackageLoader standard_loader;
    const auto h21 = standard_loader.load(
        root / "standards/technical-condition/jtg-t-h21-2011/1.0.2");
    const auto maintenance = standard_loader.load(
        root / "standards/maintenance/jtg-5120-2021/1.0.0");
    const auto extension =
        bridge_report::rating_tree::RatingTreePackageLoader().load(
            root / "standards/rating-tree/organization-bridge/1.0.1");
    ASSERT_TRUE(h21.ok());
    ASSERT_TRUE(maintenance.ok());
    ASSERT_TRUE(extension.ok());

    const auto result = bridge_report::rating_tree::RatingTreeCompiler().compile(
        *h21.package, &*maintenance.package, *extension.package);

    ASSERT_TRUE(result.ok())
        << (result.issues.empty() ? "" : result.issues.front().message);
    EXPECT_EQ(result.tree->nodes.size(), 440u);
    const auto& water =
        result.tree->nodes.at("org.bridge.defect.5_1_1_water_damage");
    ASSERT_TRUE(water.h21_indicator_id.has_value());
    EXPECT_EQ(*water.h21_indicator_id, "h21.defect.5_1_1_6");
    EXPECT_EQ(water.allowed_scales, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_EQ(water.deduction_points.at(4), 50);
    EXPECT_FALSE(result.tree->version.tree_content_checksum.empty());
}

TEST(RatingTreeCompilerIntegrationTest, Version202CompilesTheCurrentSourceTree) {
    const auto root = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT);
    bridge_report::standards::StandardPackageLoader standard_loader;
    const auto h21 = standard_loader.load(
        root / "standards/technical-condition/jtg-t-h21-2011/1.0.3");
    const auto maintenance = standard_loader.load(
        root / "standards/maintenance/jtg-5120-2021/1.0.0");
    const auto extension =
        bridge_report::rating_tree::RatingTreePackageLoader().load(
            root / "standards/rating-tree/organization-bridge/2.0.2");
    ASSERT_TRUE(h21.ok());
    ASSERT_TRUE(maintenance.ok());
    ASSERT_TRUE(extension.ok());

    const auto result = bridge_report::rating_tree::RatingTreeCompiler().compile(
        *h21.package, &*maintenance.package, *extension.package);

    ASSERT_TRUE(result.ok())
        << (result.issues.empty() ? "" : result.issues.front().message);
    EXPECT_EQ(result.tree->nodes.size(), 500u);
    const auto& crack = result.tree->nodes.at("org.bridge.defect.9_1_2_1");
    ASSERT_TRUE(crack.h21_indicator_id.has_value());
    EXPECT_EQ(*crack.h21_indicator_id, "h21.defect.9_1_2");
    EXPECT_FALSE(crack.allowed_scales.empty());
    const auto& water = result.tree->nodes.at("org.bridge.defect.9_1_1_10");
    EXPECT_TRUE(water.is_scoring);
    EXPECT_TRUE(water.uses_source_scale_descriptions);
    EXPECT_EQ(water.scale_descriptions.at(2), "局部渗水泛碱；范围＜10%");
    EXPECT_EQ(water.deduction_points.at(4), 50);
    const auto& other = result.tree->nodes.at("org.bridge.defect.9_1_1_11");
    EXPECT_FALSE(other.is_scoring);
    EXPECT_FALSE(other.h21_indicator_id.has_value());
    EXPECT_TRUE(other.allowed_scales.empty());
    EXPECT_EQ(result.tree->nodes.at("org.bridge.defect.9_1_1_1").sort_order, 10);
    EXPECT_EQ(result.tree->nodes.at("org.bridge.defect.9_1_1_10").sort_order, 100);
    EXPECT_EQ(result.tree->nodes.at("org.bridge.defect.9_1_1_11").sort_order, 110);
}

}  // namespace
