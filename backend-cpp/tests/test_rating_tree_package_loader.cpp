#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/rating_tree/RatingTreePackageLoader.hpp"

namespace {

std::filesystem::path organization_package_root() {
    return std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
        "standards/rating-tree/organization-bridge/1.0.1";
}

std::filesystem::path organization_package_root_v102() {
    return std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
        "standards/rating-tree/organization-bridge/1.0.2";
}

void write_json(const std::filesystem::path& path, const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::ofstream output(path, std::ios::binary);
    output << Json::writeString(builder, value);
}

class RatingTreePackageLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() / "bridge-report-rating-tree-loader";
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }

    void TearDown() override {
        std::filesystem::remove_all(root_);
    }

    Json::Value valid_manifest() const {
        Json::Value manifest;
        manifest["package_type"] = "rating_tree_extension";
        manifest["tree_code"] = "organization-bridge";
        manifest["tree_name"] = "单位桥梁评定树";
        manifest["package_version"] = "1.0.0";
        manifest["contract_version"] = 1;
        manifest["status"] = "active";
        manifest["content_checksum"] = "pending";
        manifest["entry_files"] = Json::Value(Json::arrayValue);
        manifest["entry_files"].append("tree.json");
        manifest["entry_files"].append("aliases.json");
        manifest["entry_files"].append("sources.json");
        return manifest;
    }

    Json::Value valid_tree() const {
        Json::Value tree;
        tree["nodes"] = Json::Value(Json::arrayValue);
        Json::Value root;
        root["id"] = "org.bridge";
        root["parent_id"] = Json::nullValue;
        root["display_name"] = "桥梁";
        root["node_type"] = "root";
        root["sort_order"] = 1;
        root["bridge_type_ids"] = Json::Value(Json::arrayValue);
        root["component_category_ids"] = Json::Value(Json::arrayValue);
        root["scoring_mode"] = "non_scoring";
        root["is_selectable"] = false;
        tree["nodes"].append(root);

        Json::Value defect;
        defect["id"] = "org.bridge.water_damage";
        defect["parent_id"] = "org.bridge";
        defect["display_name"] = "水损";
        defect["node_type"] = "defect";
        defect["sort_order"] = 2;
        defect["bridge_type_ids"] = Json::Value(Json::arrayValue);
        defect["bridge_type_ids"].append("h21.bridge_type.beam");
        defect["component_category_ids"] = Json::Value(Json::arrayValue);
        defect["component_category_ids"].append("h21.component.beam.upper_general");
        defect["scoring_mode"] = "reference_h21";
        defect["h21_indicator_id"] = "h21.defect.5_1_1_6";
        defect["is_selectable"] = true;
        defect["organization_note"] = "参照混凝土碳化执行";
        tree["nodes"].append(defect);
        return tree;
    }

    void write_valid_package() {
        Json::Value aliases;
        aliases["aliases"] = Json::Value(Json::arrayValue);
        Json::Value alias;
        alias["alias"] = "渗水泛碱";
        alias["target_node_id"] = "org.bridge.water_damage";
        alias["bridge_type_id"] = "h21.bridge_type.beam";
        alias["component_category_id"] = "h21.component.beam.upper_general";
        aliases["aliases"].append(alias);

        Json::Value sources;
        sources["sources"] = Json::Value(Json::arrayValue);
        Json::Value source;
        source["id"] = "org.source.rating-tree-screenshot";
        source["source_type"] = "organization";
        source["title"] = "单位评定树";
        sources["sources"].append(source);

        write_json(root_ / "tree.json", valid_tree());
        write_json(root_ / "aliases.json", aliases);
        write_json(root_ / "sources.json", sources);
        write_json(root_ / "manifest.json", valid_manifest());

        bridge_report::rating_tree::RatingTreePackageLoader loader;
        const auto checksum = loader.calculate_checksum(root_);
        ASSERT_TRUE(checksum.checksum.has_value());
        auto manifest = valid_manifest();
        manifest["content_checksum"] = *checksum.checksum;
        write_json(root_ / "manifest.json", manifest);
    }

    std::filesystem::path root_;
};

TEST_F(RatingTreePackageLoaderTest, LoadsAValidImmutableExtensionPackage) {
    write_valid_package();
    bridge_report::rating_tree::RatingTreePackageLoader loader;

    const auto result = loader.load(root_);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.package->manifest.tree_code, "organization-bridge");
    EXPECT_EQ(result.package->nodes.size(), 2U);
    EXPECT_EQ(result.package->aliases.size(), 1U);
}

TEST_F(RatingTreePackageLoaderTest, RejectsDuplicateNodeIds) {
    write_valid_package();
    auto tree = valid_tree();
    tree["nodes"].append(tree["nodes"][1]);
    write_json(root_ / "tree.json", tree);
    bridge_report::rating_tree::RatingTreePackageLoader loader;
    auto manifest = valid_manifest();
    const auto checksum = loader.calculate_checksum(root_);
    ASSERT_TRUE(checksum.checksum.has_value());
    manifest["content_checksum"] = *checksum.checksum;
    write_json(root_ / "manifest.json", manifest);

    const auto result = loader.load(root_);

    ASSERT_FALSE(result.ok());
    ASSERT_FALSE(result.issues.empty());
    EXPECT_EQ(result.issues.front().code, "rating_tree_node_id_duplicate");
}

TEST_F(RatingTreePackageLoaderTest, RejectsCycles) {
    write_valid_package();
    auto tree = valid_tree();
    tree["nodes"][0]["parent_id"] = "org.bridge.water_damage";
    write_json(root_ / "tree.json", tree);
    bridge_report::rating_tree::RatingTreePackageLoader loader;
    auto manifest = valid_manifest();
    const auto checksum = loader.calculate_checksum(root_);
    ASSERT_TRUE(checksum.checksum.has_value());
    manifest["content_checksum"] = *checksum.checksum;
    write_json(root_ / "manifest.json", manifest);

    const auto result = loader.load(root_);

    ASSERT_FALSE(result.ok());
    ASSERT_FALSE(result.issues.empty());
    EXPECT_EQ(result.issues.front().code, "rating_tree_cycle");
}

TEST(RatingTreePackageLoaderIntegrationTest, OrganizationPackageChecksumMatchesManifest) {
    bridge_report::rating_tree::RatingTreePackageLoader loader;
    const auto checksum = loader.calculate_checksum(organization_package_root());
    ASSERT_TRUE(checksum.ok());

    std::ifstream input(organization_package_root() / "manifest.json");
    Json::Value manifest;
    Json::CharReaderBuilder builder;
    std::string errors;
    ASSERT_TRUE(Json::parseFromStream(builder, input, &manifest, &errors));
    EXPECT_EQ(*checksum.checksum, manifest["content_checksum"].asString());
}

TEST(RatingTreePackageLoaderIntegrationTest, Version102LocksTheCorrectedH21Package) {
    bridge_report::rating_tree::RatingTreePackageLoader loader;
    const auto checksum = loader.calculate_checksum(organization_package_root_v102());
    ASSERT_TRUE(checksum.ok());

    std::ifstream input(organization_package_root_v102() / "manifest.json");
    Json::Value manifest;
    Json::CharReaderBuilder builder;
    std::string errors;
    ASSERT_TRUE(Json::parseFromStream(builder, input, &manifest, &errors));
    EXPECT_EQ(*checksum.checksum, manifest["content_checksum"].asString());

    const auto result = loader.load(organization_package_root_v102());
    ASSERT_TRUE(result.ok());
    bool references_corrected_h21 = false;
    for (const auto& [_, source] : result.package->sources) {
        if (source.source_type == "technical_condition") {
            references_corrected_h21 =
                source.reference ==
                "standards/technical-condition/jtg-t-h21-2011/1.0.3";
        }
    }
    EXPECT_TRUE(references_corrected_h21);
}

TEST(RatingTreePackageLoaderIntegrationTest, LoadsOnlyTheApprovedBridgeBranches) {
    bridge_report::rating_tree::RatingTreePackageLoader loader;
    const auto result = loader.load(organization_package_root());

    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.package->nodes.size(), 400u);
    EXPECT_TRUE(result.package->nodes.contains(
        "org.bridge.defect.5_1_1_water_damage"));
    EXPECT_TRUE(result.package->nodes.contains(
        "org.bridge.placeholder.10_6_1"));
    for (const auto& [id, node] : result.package->nodes) {
        EXPECT_FALSE(id.starts_with("org.bridge.group.11"));
        EXPECT_FALSE(id.starts_with("org.bridge.group.12"));
        EXPECT_FALSE(id.starts_with("org.bridge.group.13"));
        EXPECT_FALSE(id.starts_with("org.bridge.group.14"));
        if (node.node_type ==
            bridge_report::rating_tree::RatingTreeNodeType::placeholder) {
            EXPECT_FALSE(node.is_selectable);
            EXPECT_EQ(
                node.scoring_mode,
                bridge_report::rating_tree::RatingTreeScoringMode::non_scoring);
        }
    }
}

}  // namespace
