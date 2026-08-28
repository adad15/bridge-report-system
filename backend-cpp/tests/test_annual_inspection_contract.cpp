#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/contracts/AnnualInspectionContract.hpp"

namespace {

Json::Value read_contract_fixture(const std::string& file_name) {
    auto current_file_name = file_name;
    const auto version_marker = current_file_name.find(".v2.");
    if (version_marker != std::string::npos) {
        current_file_name.replace(version_marker, 4, ".v5.");
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

TEST(AnnualInspectionContractTest, AcceptsValidVersionFiveContractFixture) {
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
    expect_summary_contains(final_result, "contract.version: must be 5.0");
}

TEST(AnnualInspectionContractTest, AcceptsComparisonCandidateFixture) {
    const auto root = read_contract_fixture(
        "bridge_annual_inspection_data.v2.with-comparison.json");

    const auto result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_TRUE(result.ok()) << result.summary();
}

TEST(AnnualInspectionContractTest, RejectsEveryNonFiveContractVersion) {
    for (const auto* version :
         {"1.0", "1.1", "1.2", "2", "2.0", "2.1", "3", "3.0", "4", "4.0", "5"}) {
        auto root =
            read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
        root["contract"]["version"] = version;

        const auto result =
            bridge_report::contracts::validate_bridge_annual_inspection_data(root);

        EXPECT_FALSE(result.ok());
        expect_summary_contains(result, "contract.version: must be 5.0");
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
        result, "ratings: is not allowed in contract 5.0");
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
        "defects[0].defect_deduction: is not allowed in contract 5.0");
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

// 5.0 把构件解析与评分树解析整体搬进关系表。这些字段必须逐个被拒绝而不是静默忽略：
// 旧客户端的草稿会原样回传它们，忽略等于让陈旧解析结果继续盖过权威状态。
TEST(AnnualInspectionContractTest, RejectsEveryResolutionFieldRemovedInFive) {
    const std::vector<std::pair<std::string, Json::Value>> removed = {
        {"bridge_component_id", Json::Value("component-1")},
        {"standard_component_category_id", Json::Value("category-1")},
        {"resolved_structure_part", Json::Value("上部结构")},
        {"component_inventory_revision_id", Json::Value("revision-1")},
        {"component_match_method", Json::Value("manual")},
        {"component_match_confirmed_by", Json::Value("editor")},
        {"rating_tree_version_id", Json::Value("tree-version-1")},
        {"rating_tree_node_id", Json::Value("tree-node-1")},
        {"rating_tree_match_method", Json::Value("controlled_alias")},
        {"rating_tree_match_evidence", Json::Value("controlled alias")},
        {"standard_defect_indicator_id", Json::Value("indicator-1")},
    };

    for (const auto& [field, value] : removed) {
        auto root =
            read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
        root["defects"][0][field] = value;

        const auto result =
            bridge_report::contracts::validate_bridge_annual_inspection_data(root);

        EXPECT_FALSE(result.ok()) << field;
        expect_summary_contains(
            result, "defects[0]." + field + ": is not allowed in contract 5.0");
    }

    // 显式 null 同样不行：字段存在本身就是旧客户端的证据。
    auto null_root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    null_root["defects"][0]["bridge_component_id"] = Json::Value();
    const auto null_result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(null_root);
    EXPECT_FALSE(null_result.ok());
    expect_summary_contains(null_result, "defects[0].bridge_component_id");

    for (const auto* container : {"component_match_candidate_ids", "range_split_origin"}) {
        auto root =
            read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
        root["defects"][0][container] = Json::Value(Json::arrayValue);
        const auto result =
            bridge_report::contracts::validate_bridge_annual_inspection_data(root);
        EXPECT_FALSE(result.ok()) << container;
        expect_summary_contains(
            result,
            "defects[0]." + std::string(container) + ": is not allowed in contract 5.0");
    }
}

// 来源分组/指标身份不是评分树解析结果，5.0 里继续留在来源事实中。
TEST(AnnualInspectionContractTest, ValidatesSourceIndicatorIdentityFields) {
    auto root =
        read_contract_fixture("bridge_annual_inspection_data.v2.valid.json");
    auto& defect = root["defects"][0];
    defect["source_defect_group_id"] = "source-group-1";
    defect["source_defect_group_number"] = "9.1.2";
    defect["source_defect_indicator_id"] = "source-indicator-1";
    defect["source_defect_indicator_number"] = "9.1.2-1";

    EXPECT_TRUE(bridge_report::contracts::validate_bridge_annual_inspection_data(root).ok());

    for (const auto* field : {
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
