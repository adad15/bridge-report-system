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
