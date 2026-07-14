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
    const auto path = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) / "samples" / "contracts" / file_name;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open fixture: " + path.string());
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors)) {
        throw std::runtime_error("Unable to parse fixture: " + path.string() + ": " + errors);
    }

    return root;
}

void expect_summary_contains(const bridge_report::contracts::ContractValidationResult& result, const std::string& text) {
    EXPECT_NE(result.summary().find(text), std::string::npos) << result.summary();
}

const std::vector<std::string>& required_top_level_object_fields() {
    static const std::vector<std::string> fields = {
        "import_context",
        "bridge_check",
        "inspection",
    };
    return fields;
}

}  // namespace

TEST(AnnualInspectionContractTest, AcceptsValidContractFixture) {
    const auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_TRUE(result.ok()) << result.summary();
}

TEST(AnnualInspectionContractTest, AcceptsComparisonCandidateFixture) {
    const auto root = read_contract_fixture("bridge_annual_inspection_data.with-comparison.json");

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_TRUE(result.ok()) << result.summary();
}

TEST(AnnualInspectionContractTest, RejectsLegacyVersions) {
    for (const std::string version : {"1.0", "1.1"}) {
        SCOPED_TRACE(version);
        auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
        root["contract"]["version"] = version;

        const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

        EXPECT_FALSE(result.ok());
        expect_summary_contains(result, "contract.version: must be 1.2");
    }
}

TEST(AnnualInspectionContractTest, RejectsInvalidDefectScaleAndDeduction) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["defects"][0]["defect_scale"] = 0;
    root["defects"][0]["defect_deduction"] = 100.5;

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "defects[0].defect_scale");
    expect_summary_contains(result, "defects[0].defect_deduction");
}

TEST(AnnualInspectionContractTest, AcceptsNullDefectScaleAndDeduction) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["defects"][0]["defect_scale"] = Json::Value(Json::nullValue);
    root["defects"][0]["defect_deduction"] = Json::Value(Json::nullValue);

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_TRUE(result.ok()) << result.summary();
}

TEST(AnnualInspectionContractTest, RejectsMissingComponentRatings) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["ratings"].removeMember("component_ratings");

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "ratings.component_ratings");
}

TEST(AnnualInspectionContractTest, RejectsUnresolvedComponentRatingWithConfirmedScore) {
    const auto root = read_contract_fixture("bridge_annual_inspection_data.invalid-component-rating-status.json");

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "ratings.component_ratings[0].confirmed_score");
    expect_summary_contains(result, "until the reviewer makes an explicit choice");
}

TEST(AnnualInspectionContractTest, RejectsManualResolutionWithoutReason) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["ratings"]["component_ratings"][0]["score_validation_status"] = "人工接受Word值";
    root["ratings"]["component_ratings"][0]["score_resolution_reason"] = "  ";

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "ratings.component_ratings[0].score_resolution_reason");
}

TEST(AnnualInspectionContractTest, AcceptsManualResolutionWithConfirmedScoreAndReason) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["ratings"]["component_ratings"][0]["score_validation_status"] = "人工采用复算值";
    root["ratings"]["component_ratings"][0]["score_resolution_reason"] = "复算依据完整，采用规范复算值。";

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_TRUE(result.ok()) << result.summary();
}

TEST(AnnualInspectionContractTest, RejectsConsistentComponentRatingWithReason) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["ratings"]["component_ratings"][0]["score_resolution_reason"] = "不该有原因";

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "must be null when scores are consistent");
}

TEST(AnnualInspectionContractTest, RejectsDanglingDeductionDefectReference) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["ratings"]["component_ratings"][0]["deduction_defect_candidate_ids"].append("defect_9999");

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "deduction_defect_candidate_ids[1]");
    expect_summary_contains(result, "must reference an existing defect candidate");
}

TEST(AnnualInspectionContractTest, RejectsAscendingOrderedDeductions) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    auto& deductions = root["ratings"]["component_ratings"][0]["calculation_details"]["ordered_deductions"];
    deductions.clear();
    deductions.append(20.0);
    deductions.append(35.0);

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "must be sorted in descending order");
}

TEST(AnnualInspectionContractTest, RejectsDuplicateComponentRatingCandidateIds) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["ratings"]["component_ratings"].append(root["ratings"]["component_ratings"][0]);

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "ratings.component_ratings[1].candidate_id");
}

TEST(AnnualInspectionContractTest, RejectsGradeOnEvaluationPart) {
    const auto root = read_contract_fixture("bridge_annual_inspection_data.invalid-evaluation-part-grade.json");

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "ratings.evaluation_parts[0].grade");
}

TEST(AnnualInspectionContractTest, RejectsConfidenceOutsideZeroToOne) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["defects"][0]["confidence"] = 1.01;

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "defects[0].confidence");
}

TEST(AnnualInspectionContractTest, RejectsMissingRequiredTopLevelArray) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root.removeMember("comparison_candidates");

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "comparison_candidates");
}

TEST(AnnualInspectionContractTest, RejectsNonObjectRequiredTopLevelObjects) {
    for (const auto& field : required_top_level_object_fields()) {
        SCOPED_TRACE(field);
        auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
        root[field] = Json::Value(Json::arrayValue);

        const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

        EXPECT_FALSE(result.ok());
        expect_summary_contains(result, field);
    }
}

TEST(AnnualInspectionContractTest, RejectsMissingRequiredTopLevelObjects) {
    for (const auto& field : required_top_level_object_fields()) {
        SCOPED_TRACE(field);
        auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
        root.removeMember(field);

        const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

        EXPECT_FALSE(result.ok());
        expect_summary_contains(result, field);
    }
}

TEST(AnnualInspectionContractTest, RejectsMissingNestedRequiredArray) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["defects"][0].removeMember("measurements");

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "defects[0].measurements");
}

TEST(AnnualInspectionContractTest, RejectsInvalidReviewEnumsAndDuplicateIds) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["defects"][0]["review_status"] = "随便通过";
    root["defects"].append(root["defects"][0]);

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "defects[0].review_status");
    expect_summary_contains(result, "candidate_id");
}

TEST(AnnualInspectionContractTest, RejectsConfirmedMissingNumberThatIsNotReferenced) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["defects"][0]["confirmed_missing_photo_numbers"].append("2.1-99");

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "confirmed_missing_photo_numbers[0]");
}

TEST(AnnualInspectionContractTest, RejectsUnsafeArchivedPhotoPath) {
    auto root = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    root["photos"][0]["extracted_file"]["archive_relative_path"] = "../outside.jpg";

    const auto result = bridge_report::contracts::validate_bridge_annual_inspection_data(root);

    EXPECT_FALSE(result.ok());
    expect_summary_contains(result, "archive_relative_path");
}
