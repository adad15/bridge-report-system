#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/contracts/AnnualInspectionContract.hpp"

namespace {

Json::Value read_contract_fixture(const std::string& file_name) {
    auto current_file_name = file_name;
    const auto version_marker = current_file_name.find(".v2.");
    if (version_marker != std::string::npos) {
        current_file_name.replace(version_marker, 4, ".v4.");
    }
    const auto path = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
                      "samples" / "contracts" / current_file_name;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open fixture: " + path.string());
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors)) {
        throw std::runtime_error(
            "Unable to parse fixture: " + path.string() + ": " + errors);
    }
    return root;
}

void expect_summary_contains(
    const bridge_report::contracts::ContractValidationResult& result,
    const std::string& text) {
    EXPECT_NE(result.summary().find(text), std::string::npos) << result.summary();
}

}  // namespace

TEST(AnnualInspectionContractTest, AcceptsValidVersionFourContractFixture) {
    const auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_TRUE(result.ok()) << result.summary();
}

TEST(AnnualInspectionContractTest, RejectsVersionOneTwoWithoutTransitionMode) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    root["contract"]["version"] = "1.2";

    const auto final_result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);
    EXPECT_FALSE(final_result.ok());
    expect_summary_contains(final_result, "contract.version: must be 4.0");
}

TEST(AnnualInspectionContractTest, AcceptsComparisonCandidateFixture) {
    const auto root = read_contract_fixture(
        "bridge_annual_inspection_data.v2.with-comparison.json");

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_TRUE(result.ok()) << result.summary();
}

TEST(AnnualInspectionContractTest, RejectsEveryNonFourContractVersion) {
    for (const auto* version : {"1.0", "1.1", "1.2", "2", "2.0", "2.1", "3", "3.0", "4"}) {
        auto root =
            read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
        root["contract"]["version"] = version;

        const auto result =
            bridge_report::contracts::validate_bridge_annual_inspection_data(root);

        EXPECT_FALSE(result.ok());
        expect_summary_contains(result, "contract.version: must be 4.0");
    }
}

TEST(AnnualInspectionContractTest, RejectsImportedRatingsWithExactPath) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    root["ratings"]["overall"]["total_score"] = 85.61;

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(
        result, "ratings: is not allowed in contract 4.0");
}

TEST(AnnualInspectionContractTest, RejectsWordDeductionWithExactPath) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    root["defects"][0]["defect_deduction"] = 35.0;

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(
        result,
        "defects[0].defect_deduction: is not allowed in contract 4.0");
}

TEST(AnnualInspectionContractTest, RejectsLegacyPhotoReviewState) {
    for (const auto* field : {"match_status", "review_status"}) {
        auto root =
            read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
        root["photos"][0][field] = "已确认";

        const auto result =
            bridge_report::contracts::validate_bridge_annual_inspection_data(root);

        EXPECT_FALSE(result.ok()) << field;
        expect_summary_contains(result, "photos[0]." + std::string(field));
    }
}

TEST(AnnualInspectionContractTest, RejectsLegacyDefectNames) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    root["defects"][0]["structure_part"] = "上部结构";
    root["defects"][0]["component_alias"] = "2-1#梁";

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "defects[0].structure_part");
    expect_summary_contains(result, "defects[0].component_alias");
}

TEST(AnnualInspectionContractTest, AcceptsNullDatabaseAssociationFields) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    root["defects"][0]["bridge_component_id"] = Json::Value();
    root["defects"][0]["standard_component_category_id"] = Json::Value();
    root["defects"][0]["resolved_structure_part"] = Json::Value();

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_TRUE(result.ok()) << result.summary();
}

TEST(AnnualInspectionContractTest, ValidatesComponentMatchAuditFields) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    auto& defect = root["defects"][0];
    defect["component_inventory_revision_id"] = "revision-1";
    defect["component_match_candidate_ids"].append("component-1");
    defect["component_match_method"] = "manual";
    defect["component_match_confirmed_by"] = "editor";

    EXPECT_TRUE(bridge_report::contracts::validate_bridge_annual_inspection_data(root).ok());

    defect["component_match_method"] = "fuzzy";
    const auto invalid =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);
    EXPECT_FALSE(invalid.ok());
    expect_summary_contains(invalid, "defects[0].component_match_method");
}

TEST(AnnualInspectionContractTest, ValidatesRatingTreeAssociationFields) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    auto& defect = root["defects"][0];
    defect["rating_tree_version_id"] = "tree-version-1";
    defect["rating_tree_node_id"] = "tree-node-1";
    defect["rating_tree_match_method"] = "controlled_alias";
    defect["rating_tree_match_evidence"] = "controlled alias";
    defect["standard_defect_indicator_id"] = "indicator-1";
    defect["source_defect_group_id"] = "source-group-1";
    defect["source_defect_group_number"] = "9.1.2";
    defect["source_defect_indicator_id"] = "source-indicator-1";
    defect["source_defect_indicator_number"] = "9.1.2-1";

    EXPECT_TRUE(bridge_report::contracts::validate_bridge_annual_inspection_data(root).ok());

    for (const auto* field : {
             "rating_tree_version_id",
             "rating_tree_node_id",
             "rating_tree_match_evidence",
             "standard_defect_indicator_id",
             "source_defect_group_id",
             "source_defect_group_number",
             "source_defect_indicator_id",
             "source_defect_indicator_number",
         }) {
        auto invalid_root =
            read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
        invalid_root["defects"][0][field] = "   ";
        const auto invalid =
            bridge_report::contracts::validate_bridge_annual_inspection_data(
                invalid_root);
        EXPECT_FALSE(invalid.ok()) << field;
        expect_summary_contains(invalid, "defects[0]." + std::string(field));
    }

    defect["rating_tree_match_method"] = "guessed";
    const auto invalid_method =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);
    EXPECT_FALSE(invalid_method.ok());
    expect_summary_contains(
        invalid_method, "defects[0].rating_tree_match_method");
}

TEST(AnnualInspectionContractTest, ValidatesOptionalRangeSplitOrigin) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    auto& origin = root["defects"][0]["range_split_origin"];
    origin["operation_id"] = "operation-1";
    origin["source_candidate_id"] = "defect_0001";
    origin["source_component_number"] = "1-1#板~1-25#板";
    origin["expanded_component_number"] = "1-7#板";
    origin["split_index"] = 7;
    origin["split_count"] = 25;
    origin["operated_by_user_id"] = "user-1";
    origin["operated_at"] = "2026-07-24T16:00:00+08:00";

    EXPECT_TRUE(bridge_report::contracts::validate_bridge_annual_inspection_data(root).ok());

    origin["split_index"] = 26;
    const auto invalid =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);
    EXPECT_FALSE(invalid.ok());
    expect_summary_contains(invalid, "defects[0].range_split_origin");
}

TEST(AnnualInspectionContractTest, RejectsUnknownRangeSplitOriginMember) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    auto& origin = root["defects"][0]["range_split_origin"];
    origin["operation_id"] = "operation-1";
    origin["source_candidate_id"] = "defect_0001";
    origin["source_component_number"] = "1-1#板~1-25#板";
    origin["expanded_component_number"] = "1-7#板";
    origin["split_index"] = 7;
    origin["split_count"] = 25;
    origin["operated_by_user_id"] = "user-1";
    origin["operated_at"] = "2026-07-24T16:00:00+08:00";
    origin["unexpected"] = true;

    const auto invalid =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);
    EXPECT_FALSE(invalid.ok());
    expect_summary_contains(
        invalid, "defects[0].range_split_origin.unexpected");
}

TEST(AnnualInspectionContractTest, AcceptsManualSourceWithoutWordCoordinates) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    root["defects"][0]["source_ref"] = Json::Value(Json::objectValue);
    root["defects"][0]["source_ref"]["source_type"] = "manual";

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_TRUE(result.ok()) << result.summary();
}

TEST(AnnualInspectionContractTest, RejectsUnknownSourceTypeWithExactPath) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    root["defects"][0]["source_ref"]["source_type"] = "spreadsheet";

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(
        result, "defects[0].source_ref.source_type");
}

TEST(AnnualInspectionContractTest, RejectsInvalidDefectScale) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    root["defects"][0]["defect_scale"] = 0;

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "defects[0].defect_scale");
}

TEST(AnnualInspectionContractTest, RejectsMissingRequiredTopLevelMembers) {
    for (const auto* field : {
             "import_context",
             "bridge_check",
             "inspection",
             "defects",
             "photos",
             "comparison_candidates",
             "report_text_candidates",
             "warnings",
             "errors",
         }) {
        auto root =
            read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
        root.removeMember(field);

        const auto result =
            bridge_report::contracts::validate_bridge_annual_inspection_data(root);

        EXPECT_FALSE(result.ok()) << field;
        expect_summary_contains(result, field);
    }
}

TEST(AnnualInspectionContractTest, RejectsMissingNestedArrays) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    root["defects"][0].removeMember("measurements");
    root["photos"][0].removeMember("warnings");

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "defects[0].measurements");
    expect_summary_contains(result, "photos[0].warnings");
}

TEST(AnnualInspectionContractTest, RejectsDuplicateCandidateIds) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    root["defects"].append(root["defects"][0]);

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "defects[1].candidate_id");
}

TEST(AnnualInspectionContractTest, RejectsRemovedConfirmedMissingPhotoNumbers) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    root["defects"][0]["confirmed_missing_photo_numbers"].append("9.9-9");

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(
        result, "defects[0].confirmed_missing_photo_numbers");
}

TEST(AnnualInspectionContractTest, RejectsUnsafeArchivedPhotoPath) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    root["photos"][0]["extracted_file"]["archive_relative_path"] =
        "../outside.jpg";

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(
        result, "photos[0].extracted_file.archive_relative_path");
}

TEST(AnnualInspectionContractTest, AcceptsRangeMeasurement) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    auto& measurement = root["defects"][0]["measurements"][0];
    measurement["value_type"] = "range";
    measurement["value"] = Json::Value();
    measurement["minimum_value"] = 0.5;
    measurement["maximum_value"] = 4.0;
    measurement["is_approximate"] = false;
    measurement["source_text"] = "0.5~4.0m";

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_TRUE(result.ok()) << result.summary();
}

TEST(AnnualInspectionContractTest, RejectsInvalidMeasurementInvariantsWithExactPath) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    auto& measurement = root["defects"][0]["measurements"][0];
    measurement["value_type"] = "range";
    measurement["value"] = 1.0;
    measurement["minimum_value"] = 4.0;
    measurement["maximum_value"] = 0.5;
    measurement["is_approximate"] = false;

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "defects[0].measurements[0]");
}
