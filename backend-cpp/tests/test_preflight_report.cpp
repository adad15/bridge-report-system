#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/review/PreflightReport.hpp"

namespace {

constexpr const char* kRevision = "00000000-0000-0000-0000-000000000201";

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

void settle(Json::Value& data) {
    auto& defect = data["defects"][0];
    defect["bridge_component_id"] = "00000000-0000-0000-0000-000000000101";
    defect["standard_component_category_id"] = "category-main-girder";
    defect["resolved_structure_part"] = "上部结构";
    defect["component_inventory_revision_id"] = kRevision;
    defect["review_status"] = "已确认";
    defect["group_review_status"] = "已确认";
    data["photos"][0]["match_status"] = "已确认";
    data["photos"][0]["review_status"] = "已确认";
    auto& reference = defect["photo_references"][0];
    reference["resolution"] = "matched";
    reference["photo_candidate_id"] = data["photos"][0]["candidate_id"];
    reference["resolved_defect_candidate_id"] = defect["candidate_id"];
}

bridge_report::review::PreflightContext context() {
    bridge_report::review::PreflightContext value;
    value.import_status = "待校对";
    value.record_system_number = "DRJL-000001";
    value.bridge_system_number = "QL-000001";
    value.inspection_year = 2026;
    value.component_inventory_revision_id = kRevision;
    value.component_inventory_confirmed = true;
    return value;
}

bool has_code(
    const std::vector<bridge_report::review::PreflightIssue>& issues,
    const std::string& code) {
    for (const auto& issue : issues) if (issue.code == code) return true;
    return false;
}

}  // namespace

TEST(PreflightReportTest, AllGreenForSettledVersionTwoFacts) {
    auto data = fixture();
    settle(data);
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(report.can_confirm);
    EXPECT_TRUE(report.blocking_errors.empty());
}

TEST(PreflightReportTest, RejectsImportedRatingsAtContractBoundary) {
    auto data = fixture();
    settle(data);
    data["ratings"]["overall"]["total_score"] = 42;
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(has_code(report.blocking_errors, "contract_validation_failed"));
}

TEST(PreflightReportTest, PendingDefectBlocks) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["review_status"] = "待确认";
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(has_code(report.blocking_errors, "candidate_pending_review"));
}

TEST(PreflightReportTest, MissingRequiredComponentNumberBlocks) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["component_number"] = "";
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(has_code(report.blocking_errors, "defect_missing_required_field"));
}

TEST(PreflightReportTest, InvalidScaleBlocks) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["defect_scale"] = Json::Value(Json::nullValue);
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(has_code(report.blocking_errors, "defect_scale_required"));
}

TEST(PreflightReportTest, ActualComponentAssociationMustUseLatestRevision) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["component_inventory_revision_id"] = "old-revision";
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(has_code(report.blocking_errors, "defect_component_match_required"));
}

TEST(PreflightReportTest, MarkedMissingDefectDoesNotBlockConfirmation) {
    auto data = fixture();
    settle(data);
    auto& defect = data["defects"][0];
    // 绑定界面标记缺失：未关联实际构件，但显式标记 → 不再阻塞。
    defect["bridge_component_id"] = "";
    defect["standard_component_category_id"] = "";
    defect["resolved_structure_part"] = "";
    defect["component_inventory_revision_id"] = "";
    defect["component_match_method"] = "missing";
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_FALSE(has_code(report.blocking_errors, "defect_component_match_required"));
}

TEST(PreflightReportTest, GroupConfirmationBlocks) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["group_review_status"] = "待确认";
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(has_code(report.blocking_errors, "group_confirmation_required"));
}

TEST(PreflightReportTest, MissingPhotoRequiresAcknowledgement) {
    auto data = fixture();
    settle(data);
    data["photos"] = Json::Value(Json::arrayValue);
    auto& reference = data["defects"][0]["photo_references"][0];
    reference["resolution"] = "pending";
    reference["photo_candidate_id"] = Json::Value();
    reference["resolved_defect_candidate_id"] = Json::Value();
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(has_code(report.blocking_errors, "missing_photo_confirmation_required"));
}

TEST(PreflightReportTest, AcknowledgedMissingPhotoDoesNotBlock) {
    auto data = fixture();
    settle(data);
    data["photos"] = Json::Value(Json::arrayValue);
    auto& reference = data["defects"][0]["photo_references"][0];
    reference["resolution"] = "missing";
    reference["photo_candidate_id"] = Json::Value();
    reference["resolved_defect_candidate_id"] = Json::Value();
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(report.can_confirm);
    EXPECT_TRUE(has_code(report.warnings, "defect_without_photo"));
}

TEST(PreflightReportTest, ResolvedPhotoNeedsArchivedFile) {
    auto data = fixture();
    settle(data);
    data["photos"][0]["extracted_file"]["archive_relative_path"] = "";
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(
        has_code(report.blocking_errors, "photo_archive_missing") ||
        has_code(report.blocking_errors, "contract_validation_failed"));
}

TEST(PreflightReportTest, ImportContextMismatchBlocks) {
    auto data = fixture();
    settle(data);
    auto changed = context();
    changed.record_system_number = "DRJL-OTHER";
    const auto report = bridge_report::review::build_preflight_report(data, changed);
    EXPECT_TRUE(has_code(report.blocking_errors, "import_context_mismatch"));
}

TEST(PreflightReportTest, RevisionFlagIsReportedWithoutBlocking) {
    auto data = fixture();
    settle(data);
    auto changed = context();
    changed.has_current_annual_facts = true;
    const auto report = bridge_report::review::build_preflight_report(data, changed);
    EXPECT_TRUE(report.can_confirm);
    EXPECT_TRUE(report.requires_revision_confirmation);
    EXPECT_TRUE(report.to_json()["requires_revision_confirmation"].asBool());
}

TEST(PreflightReportTest, WrongImportStatusBlocksBeforeBusinessChecks) {
    auto data = fixture();
    settle(data);
    auto changed = context();
    changed.import_status = "已确认";
    const auto report = bridge_report::review::build_preflight_report(data, changed);
    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(has_code(report.blocking_errors, "import_record_wrong_status"));
}

TEST(PreflightReportTest, InvalidContractShortCircuitsDependentChecks) {
    auto data = fixture();
    data["contract"]["version"] = "1.2";
    const auto report = bridge_report::review::build_preflight_report(data, context());
    ASSERT_EQ(report.blocking_errors.size(), 1u);
    EXPECT_EQ(report.blocking_errors[0].code, "contract_validation_failed");
}

TEST(PreflightReportTest, PendingPhotoBlocks) {
    auto data = fixture();
    settle(data);
    data["photos"][0]["review_status"] = "待确认";
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(has_code(report.blocking_errors, "candidate_pending_review"));
}

TEST(PreflightReportTest, EachRequiredDefectBusinessFieldIsChecked) {
    for (const auto* field : {"component_name", "component_number", "defect_location", "defect_type", "defect_description"}) {
        auto data = fixture();
        settle(data);
        data["defects"][0][field] = "";
        const auto report = bridge_report::review::build_preflight_report(data, context());
        EXPECT_FALSE(report.can_confirm) << field;
        EXPECT_TRUE(
            has_code(report.blocking_errors, "defect_missing_required_field") ||
            has_code(report.blocking_errors, "contract_validation_failed")) << field;
    }
}

TEST(PreflightReportTest, UnconfirmedInventoryBlocksEverySettledDefect) {
    auto data = fixture();
    settle(data);
    auto changed = context();
    changed.component_inventory_confirmed = false;
    const auto report = bridge_report::review::build_preflight_report(data, changed);
    EXPECT_TRUE(has_code(report.blocking_errors, "component_inventory_unconfirmed"));
}

TEST(PreflightReportTest, ConfirmedPhotoMustResolveToASettledDefect) {
    auto data = fixture();
    settle(data);
    data["photos"][0]["linked_defect_candidate_id"] = "defect_missing";
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(
        has_code(report.blocking_errors, "photo_link_unresolved") ||
        has_code(report.blocking_errors, "contract_validation_failed"));
}

TEST(PreflightReportTest, DefectWithoutPhotoNumberDoesNotWarn) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["photo_references"] = Json::Value(Json::arrayValue);
    data["photos"] = Json::Value(Json::arrayValue);
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(report.can_confirm);
    EXPECT_FALSE(has_code(report.warnings, "defect_without_photo"));
}

TEST(PreflightReportTest, IgnoredUnreferencedPhotoProducesAWarning) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["photo_references"] = Json::Value(Json::arrayValue);
    data["photos"][0]["linked_defect_candidate_id"] = Json::Value(Json::nullValue);
    data["photos"][0]["match_status"] = "已忽略";
    data["photos"][0]["review_status"] = "已忽略";
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(report.can_confirm);
    EXPECT_TRUE(has_code(report.warnings, "unreferenced_photo_ignored"));
}

TEST(PreflightReportTest, UnstructuredMeasurementTextIsRetainedAsWarning) {
    auto data = fixture();
    settle(data);
    data["defects"][0]["measurements"] = Json::Value(Json::arrayValue);
    data["defects"][0]["measurement_text"] = "现场量测值待复核";
    const auto report = bridge_report::review::build_preflight_report(data, context());
    EXPECT_TRUE(report.can_confirm);
    EXPECT_TRUE(has_code(report.warnings, "measurement_unstructured_kept"));
}

TEST(PreflightReportTest, ToJsonPreservesIssueTargetsAndNulls) {
    bridge_report::review::PreflightReport report;
    report.blocking_errors.push_back({"candidate_pending_review", "待确认", "defect_0001"});
    report.warnings.push_back({"measurement_unstructured_kept", "保留原文", ""});
    const auto json = report.to_json();
    EXPECT_EQ(json["blocking_errors"][0]["target_candidate_id"].asString(), "defect_0001");
    EXPECT_TRUE(json["warnings"][0]["target_candidate_id"].isNull());
}
