#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/review/DraftValidation.hpp"

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

constexpr const char* kSystemNumber = "DRJL-000001";

}  // namespace

TEST(DraftValidationTest, AcceptsValidDraftWhenPendingReview) {
    const auto body = read_contract_fixture("bridge_annual_inspection_data.valid.json");

    const auto result = bridge_report::review::validate_review_draft(body, kSystemNumber, "待校对");

    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.code.empty());
    EXPECT_TRUE(result.issues.empty());
}

TEST(DraftValidationTest, RejectsWhenImportStatusIsNotPendingReview) {
    const auto body = read_contract_fixture("bridge_annual_inspection_data.valid.json");

    const auto result = bridge_report::review::validate_review_draft(body, kSystemNumber, "已确认");

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "import_record_not_editable");
}

TEST(DraftValidationTest, RejectsWhenContractValidationFails) {
    auto body = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    body.removeMember("ratings");

    const auto result = bridge_report::review::validate_review_draft(body, kSystemNumber, "待校对");

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "contract_validation_failed");
    EXPECT_FALSE(result.issues.empty());
}

TEST(DraftValidationTest, RejectsWhenImportContextSystemNumberMismatches) {
    const auto body = read_contract_fixture("bridge_annual_inspection_data.valid.json");

    const auto result = bridge_report::review::validate_review_draft(body, "DRJL-000002", "待校对");

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "import_context_mismatch");
}
