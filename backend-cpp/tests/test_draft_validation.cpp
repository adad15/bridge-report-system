#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/review/DraftValidation.hpp"

namespace {

Json::Value fixture() {
    const auto path = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
        "samples/contracts/bridge_annual_inspection_data.v3.valid.json";
    std::ifstream input(path, std::ios::binary);
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!input || !Json::parseFromStream(builder, input, &root, &errors)) {
        throw std::runtime_error("unable to load v2 fixture: " + errors);
    }
    return root;
}

}  // namespace

TEST(DraftValidationTest, AcceptsValidVersionTwoDraftWhenPendingReview) {
    const auto result = bridge_report::review::validate_review_draft(
        fixture(), "DRJL-000001", "待校对");
    EXPECT_TRUE(result.ok) << result.message;
}

TEST(DraftValidationTest, RejectsNonEditableImportStatusFirst) {
    const auto result = bridge_report::review::validate_review_draft(
        fixture(), "DRJL-000001", "已确认");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "import_record_not_editable");
}

TEST(DraftValidationTest, RejectsImportContextMismatch) {
    const auto result = bridge_report::review::validate_review_draft(
        fixture(), "DRJL-OTHER", "待校对");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "import_context_mismatch");
}

TEST(DraftValidationTest, RejectsImportedRatingProjection) {
    auto data = fixture();
    data["ratings"]["overall"]["total_score"] = 85.61;
    const auto result = bridge_report::review::validate_review_draft(
        data, "DRJL-000001", "待校对");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "contract_validation_failed");
}

TEST(DraftValidationTest, RejectsPartialActualComponentAssociation) {
    auto data = fixture();
    data["defects"][0]["bridge_component_id"] = "component-1";
    const auto result = bridge_report::review::validate_review_draft(
        data, "DRJL-000001", "待校对");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "defect_component_mapping_invalid");
}

TEST(WarningsOnlyScopeTest, AllowsBusinessEditsOnWarningDefect) {
    auto stored = fixture();
    stored["defects"][0]["warnings"].append(Json::Value(Json::objectValue));
    stored["defects"][0]["warnings"][0]["code"] = "defect_location_missing";
    stored["defects"][0]["warnings"][0]["message"] = "位置待确认";
    stored["defects"][0]["warnings"][0]["severity"] = "warning";
    auto next = stored;
    next["defects"][0]["defect_location"] = "第二跨梁底";
    next["defects"][0]["review_status"] = "已修改";

    Json::Value normalized;
    const auto result = bridge_report::review::validate_warnings_only_scope(
        stored, next, &normalized);
    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(normalized["defects"][0]["defect_location"].asString(), "第二跨梁底");
}

TEST(WarningsOnlyScopeTest, RejectsDeletingAWarningDefect) {
    auto stored = fixture();
    stored["defects"][0]["warnings"].append(Json::Value(Json::objectValue));
    auto next = stored;
    next["defects"] = Json::Value(Json::arrayValue);
    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "reopen_scope_violation");
}

TEST(DraftValidationTest, BuildsServerOwnedManualDefectAudit) {
    const auto stored = fixture();
    auto next = stored;
    auto added = stored["defects"][0];
    added["candidate_id"] = "manual_defect_0001";
    next["defects"].append(added);
    const auto event = bridge_report::review::build_defect_change_audit_event(
        stored, next, "editor");
    EXPECT_EQ(event["added_candidate_ids"][0].asString(), "manual_defect_0001");
    EXPECT_EQ(event["actor_username"].asString(), "editor");
}

TEST(DraftValidationTest, RejectsEveryLegacyContractVersion) {
    for (const auto* version : {"1.0", "1.1", "1.2"}) {
        auto data = fixture();
        data["contract"]["version"] = version;
        const auto result = bridge_report::review::validate_review_draft(
            data, "DRJL-000001", "待校对");
        EXPECT_FALSE(result.ok) << version;
        EXPECT_EQ(result.code, "contract_validation_failed") << version;
    }
}

TEST(DraftValidationTest, RejectsDuplicateDefectCandidateIds) {
    auto data = fixture();
    data["defects"].append(data["defects"][0]);
    const auto result = bridge_report::review::validate_review_draft(
        data, "DRJL-000001", "待校对");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "contract_validation_failed");
}

TEST(DraftHasWarningDefectsTest, ReadsOnlyNonEmptyDefectWarningArrays) {
    auto data = fixture();
    EXPECT_FALSE(bridge_report::review::draft_has_warning_defects(data));
    data["photos"][0]["warnings"].append(Json::Value(Json::objectValue));
    EXPECT_FALSE(bridge_report::review::draft_has_warning_defects(data));
    data["defects"][0]["warnings"].append(Json::Value(Json::objectValue));
    EXPECT_TRUE(bridge_report::review::draft_has_warning_defects(data));
}

TEST(WarningsOnlyScopeTest, RejectsChangingDefectWithoutStoredWarning) {
    const auto stored = fixture();
    auto next = stored;
    next["defects"][0]["defect_description"] = "客户端越权修改";
    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "reopen_scope_violation");
}

TEST(WarningsOnlyScopeTest, ClientCannotFakeAWarningToUnlockDefect) {
    const auto stored = fixture();
    auto next = stored;
    next["defects"][0]["warnings"].append(Json::Value(Json::objectValue));
    next["defects"][0]["defect_description"] = "伪造警告后修改";
    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "reopen_scope_violation");
}

TEST(WarningsOnlyScopeTest, RejectsChangingSourceEvidenceOnWarningDefect) {
    auto stored = fixture();
    stored["defects"][0]["warnings"].append(Json::Value(Json::objectValue));
    auto next = stored;
    next["defects"][0]["source_ref"]["raw_row_text"] = "被篡改的来源证据";
    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "reopen_scope_violation");
}

TEST(WarningsOnlyScopeTest, RejectsChangingPhotosWhileFixingWarningDefect) {
    auto stored = fixture();
    stored["defects"][0]["warnings"].append(Json::Value(Json::objectValue));
    auto next = stored;
    next["photos"][0]["review_status"] = "已确认";
    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "reopen_scope_violation");
}

TEST(DraftValidationTest, AuditIsNullWhenDefectSetIsUnchanged) {
    const auto data = fixture();
    EXPECT_TRUE(
        bridge_report::review::build_defect_change_audit_event(data, data, "editor").isNull());
}
