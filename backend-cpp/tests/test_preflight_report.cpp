#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/review/PreflightReport.hpp"
#include "support/review_fixtures.hpp"

namespace {

using bridge_report::test_support::confirm_all_candidates;
using bridge_report::review::build_preflight_report;
using bridge_report::review::PreflightContext;
using bridge_report::review::PreflightIssue;
using bridge_report::review::PreflightReport;

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

Json::Value valid_data() {
    return read_contract_fixture("bridge_annual_inspection_data.valid.json");
}

constexpr const char* kRecordSystemNumber = "DRJL-000001";
constexpr const char* kBridgeSystemNumber = "QL-000001";
constexpr int kInspectionYear = 2026;

PreflightContext base_context() {
    PreflightContext context;
    context.import_status = "待校对";
    context.record_system_number = kRecordSystemNumber;
    context.bridge_system_number = kBridgeSystemNumber;
    context.inspection_year = kInspectionYear;
    context.has_current_annual_facts = false;
    return context;
}

bool has_blocking_code(const PreflightReport& report, const std::string& code) {
    return std::any_of(report.blocking_errors.begin(), report.blocking_errors.end(), [&](const PreflightIssue& issue) {
        return issue.code == code;
    });
}

bool has_warning_code(const PreflightReport& report, const std::string& code) {
    return std::any_of(report.warnings.begin(), report.warnings.end(), [&](const PreflightIssue& issue) {
        return issue.code == code;
    });
}

const PreflightIssue* find_blocking(const PreflightReport& report, const std::string& code) {
    for (const auto& issue : report.blocking_errors) {
        if (issue.code == code) {
            return &issue;
        }
    }
    return nullptr;
}

const PreflightIssue* find_warning(const PreflightReport& report, const std::string& code) {
    for (const auto& issue : report.warnings) {
        if (issue.code == code) {
            return &issue;
        }
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// All-green scenario
// ---------------------------------------------------------------------------

TEST(PreflightReportTest, AllGreenWhenEverythingConfirmed) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_TRUE(report.can_confirm);
    EXPECT_TRUE(report.blocking_errors.empty());
}

TEST(PreflightReportTest, RevisionConfirmationRequiredDoesNotBlockCanConfirm) {
    auto data = valid_data();
    confirm_all_candidates(data);

    auto context = base_context();
    context.has_current_annual_facts = true;

    const auto report = build_preflight_report(data, context);

    EXPECT_TRUE(report.requires_revision_confirmation);
    EXPECT_TRUE(report.can_confirm);
    EXPECT_TRUE(report.blocking_errors.empty());
}

TEST(PreflightReportTest, RevisionConfirmationNotRequiredWhenNoCurrentFacts) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.requires_revision_confirmation);
}

// ---------------------------------------------------------------------------
// Blocking errors
// ---------------------------------------------------------------------------

TEST(PreflightReportTest, ImportRecordWrongStatusBlocks) {
    auto data = valid_data();
    confirm_all_candidates(data);

    auto context = base_context();
    context.import_status = "已确认";

    const auto report = build_preflight_report(data, context);

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "import_record_wrong_status"));
}

TEST(PreflightReportTest, ContractValidationFailedBlocks) {
    auto data = valid_data();
    data.removeMember("ratings");

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "contract_validation_failed"));
}

TEST(PreflightReportTest, GroupConfirmationRequiredBlocks) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["group_review_status"] = "待确认";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(has_blocking_code(report, "group_confirmation_required"));
}

TEST(PreflightReportTest, MissingPhotoNeedsExplicitAcknowledgement) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["photo_numbers"].append("2.1-99");

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(has_blocking_code(report, "missing_photo_confirmation_required"));
}

TEST(PreflightReportTest, MissingPhotoAcknowledgementAllowsConfirmation) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["photo_numbers"].append("2.1-99");
    data["defects"][0]["confirmed_missing_photo_numbers"].append("2.1-99");

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(has_blocking_code(report, "missing_photo_confirmation_required"));
}

TEST(PreflightReportTest, ResolvedPhotoWithoutArchivePathBlocks) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["photos"][0]["extracted_file"]["archive_relative_path"] = Json::Value(Json::nullValue);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(has_blocking_code(report, "photo_archive_missing"));
}

TEST(PreflightReportTest, ImportContextMismatchOnRecordSystemNumber) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["import_context"]["import_record_system_number"] = "DRJL-999999";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "import_context_mismatch"));
}

TEST(PreflightReportTest, ImportContextMismatchOnBridgeSystemNumber) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["bridge_check"]["selected_bridge_system_number"] = "QL-999999";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "import_context_mismatch"));
}

TEST(PreflightReportTest, ImportContextMismatchOnInspectionYear) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["inspection"]["inspection_year"] = 2025;

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "import_context_mismatch"));
}

TEST(PreflightReportTest, ImportContextMismatchSkippedWhenContextYearAbsent) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["inspection"]["inspection_year"] = 2025;

    auto context = base_context();
    context.inspection_year.reset();

    const auto report = build_preflight_report(data, context);

    EXPECT_FALSE(has_blocking_code(report, "import_context_mismatch"));
}

TEST(PreflightReportTest, CandidatePendingReviewForDefect) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["review_status"] = "待确认";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    const auto* issue = find_blocking(report, "candidate_pending_review");
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->target_candidate_id, "defect_0001");
}

TEST(PreflightReportTest, CandidatePendingReviewForPhoto) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["photos"][0]["review_status"] = "待确认";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    bool found = false;
    for (const auto& issue : report.blocking_errors) {
        if (issue.code == "candidate_pending_review" && issue.target_candidate_id == "photo_0001") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(PreflightReportTest, CandidatePendingReviewForRatingOverall) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["overall"]["review_status"] = "待确认";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    bool found = false;
    for (const auto& issue : report.blocking_errors) {
        if (issue.code == "candidate_pending_review" && issue.target_candidate_id == "ratings.overall") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(PreflightReportTest, CandidatePendingReviewForRatingStructurePart) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["structure_parts"][1]["review_status"] = "待确认";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    bool found = false;
    for (const auto& issue : report.blocking_errors) {
        if (issue.code == "candidate_pending_review" && issue.target_candidate_id == "ratings.structure_parts[1]") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(PreflightReportTest, CandidatePendingReviewForRatingEvaluationPart) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["evaluation_parts"][2]["review_status"] = "待确认";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    bool found = false;
    for (const auto& issue : report.blocking_errors) {
        if (issue.code == "candidate_pending_review" && issue.target_candidate_id == "ratings.evaluation_parts[2]") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(PreflightReportTest, DefectMissingRequiredFieldStructurePart) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["structure_part"] = "";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    const auto* issue = find_blocking(report, "defect_missing_required_field");
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->target_candidate_id, "defect_0001");
}

TEST(PreflightReportTest, DefectMissingRequiredFieldComponentNameMissingKey) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0].removeMember("component_name");

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "defect_missing_required_field"));
}

TEST(PreflightReportTest, DefectMissingRequiredFieldDefectType) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["defect_type"] = "";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "defect_missing_required_field"));
}

TEST(PreflightReportTest, DefectMissingRequiredFieldDescription) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["defect_description"] = "";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "defect_missing_required_field"));
}

TEST(PreflightReportTest, DefectMissingRequiredFieldSkippedWhenPending) {
    auto data = valid_data();
    // Leave defect as 待确认 (default) with a blank required field; the missing-field
    // check should not fire for pending candidates (candidate_pending_review covers it instead).
    data["defects"][0]["structure_part"] = "";
    confirm_all_candidates(data);
    data["defects"][0]["review_status"] = "待确认";
    data["defects"][0]["structure_part"] = "";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(has_blocking_code(report, "defect_missing_required_field"));
}

TEST(PreflightReportTest, PhotoLinkUnresolvedWhenLinkedDefectIdMissing) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["photos"][0]["linked_defect_candidate_id"] = "";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    const auto* issue = find_blocking(report, "photo_link_unresolved");
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->target_candidate_id, "photo_0001");
}

TEST(PreflightReportTest, PhotoLinkUnresolvedWhenLinkedDefectIdIsNull) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["photos"][0]["linked_defect_candidate_id"] = Json::Value(Json::nullValue);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "photo_link_unresolved"));
}

TEST(PreflightReportTest, MissingLinkedDefectFailsContractValidation) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["photos"][0]["linked_defect_candidate_id"] = "defect_9999";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "contract_validation_failed"));
}

TEST(PreflightReportTest, PhotoLinkUnresolvedWhenLinkedDefectIgnored) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["review_status"] = "已忽略";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "photo_link_unresolved"));
}

TEST(PreflightReportTest, PhotoLinkUnresolvedWhenLinkedDefectPending) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["photos"][0]["review_status"] = "已确认";
    data["defects"][0]["review_status"] = "待确认";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "photo_link_unresolved"));
    // Also expect candidate_pending_review for the pending defect itself.
    ASSERT_TRUE(has_blocking_code(report, "candidate_pending_review"));
}

TEST(PreflightReportTest, PhotoLinkResolvedFineWhenMatchStatusUnrelated) {
    auto data = valid_data();
    confirm_all_candidates(data);
    // 未关联 photos should not trigger photo_link_unresolved even without a link.
    data["photos"][0]["match_status"] = "未关联";
    data["photos"][0]["linked_defect_candidate_id"] = "";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(has_blocking_code(report, "photo_link_unresolved"));
}

TEST(PreflightReportTest, RatingOverallMissingTotalScore) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["overall"].removeMember("total_score");

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    const auto* issue = find_blocking(report, "rating_overall_missing");
    ASSERT_NE(issue, nullptr);
}

TEST(PreflightReportTest, RatingOverallMissingGradeEmptyString) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["overall"]["overall_grade"] = "";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "rating_overall_missing"));
}

TEST(PreflightReportTest, RatingOverallMissingGradeNonString) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["overall"]["overall_grade"] = 2;

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "rating_overall_missing"));
}

TEST(PreflightReportTest, RatingOverallMissingTotalScoreNonNumeric) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["overall"]["total_score"] = "not-a-number";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    ASSERT_TRUE(has_blocking_code(report, "rating_overall_missing"));
}

// ---------------------------------------------------------------------------
// Warnings
// ---------------------------------------------------------------------------

TEST(PreflightReportTest, DefectWithoutPhotoWhenPhotoNumbersEmpty) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["photo_numbers"] = Json::Value(Json::arrayValue);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_TRUE(report.can_confirm);
    const auto* issue = find_warning(report, "defect_without_photo");
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->target_candidate_id, "defect_0001");
}

TEST(PreflightReportTest, DefectWithoutPhotoWhenNumberHasNoConfirmedLinkedPhoto) {
    auto data = valid_data();
    confirm_all_candidates(data);
    // photo_numbers references "2.1-1" but no confirmed photo links to defect_0001 anymore.
    data["photos"][0]["linked_defect_candidate_id"] = Json::Value(Json::nullValue);
    data["photos"][0]["match_status"] = "未关联";

    const auto report = build_preflight_report(data, base_context());

    ASSERT_TRUE(has_warning_code(report, "defect_without_photo"));
}

TEST(PreflightReportTest, DefectWithoutPhotoAbsentWhenLinkedPhotoConfirmed) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(has_warning_code(report, "defect_without_photo"));
}

TEST(PreflightReportTest, UnreferencedPhotoIgnoredWhenReviewStatusIgnored) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["photos"][0]["review_status"] = "已忽略";
    // Avoid tripping photo_link_unresolved/candidate_pending_review distractions; defect still confirmed.

    const auto report = build_preflight_report(data, base_context());

    const auto* issue = find_warning(report, "unreferenced_photo_ignored");
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->target_candidate_id, "photo_0001");
}

TEST(PreflightReportTest, UnreferencedPhotoIgnoredWhenConfirmedButUnlinked) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["photos"][0]["match_status"] = "未关联";
    data["photos"][0]["linked_defect_candidate_id"] = "";

    const auto report = build_preflight_report(data, base_context());

    ASSERT_TRUE(has_warning_code(report, "unreferenced_photo_ignored"));
}

TEST(PreflightReportTest, UnreferencedPhotoIgnoredAbsentWhenLinkedAndConfirmed) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(has_warning_code(report, "unreferenced_photo_ignored"));
}

TEST(PreflightReportTest, MeasurementUnstructuredKeptWhenMeasurementsEmpty) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["measurement_text"] = "L=0.8m";
    data["defects"][0]["measurements"] = Json::Value(Json::arrayValue);

    const auto report = build_preflight_report(data, base_context());

    const auto* issue = find_warning(report, "measurement_unstructured_kept");
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->target_candidate_id, "defect_0001");
}

TEST(PreflightReportTest, MeasurementUnstructuredKeptAbsentWhenMeasurementsPresent) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(has_warning_code(report, "measurement_unstructured_kept"));
}

TEST(PreflightReportTest, MeasurementUnstructuredKeptAbsentWhenMeasurementTextEmpty) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["measurement_text"] = "";
    data["defects"][0]["measurements"] = Json::Value(Json::arrayValue);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(has_warning_code(report, "measurement_unstructured_kept"));
}

TEST(PreflightReportTest, RatingPartsIncompleteWhenStructurePartsTooFew) {
    auto data = valid_data();
    confirm_all_candidates(data);
    Json::Value structure_parts(Json::arrayValue);
    structure_parts.append(data["ratings"]["structure_parts"][0]);
    data["ratings"]["structure_parts"] = structure_parts;

    const auto report = build_preflight_report(data, base_context());

    const auto* issue = find_warning(report, "rating_parts_incomplete");
    ASSERT_NE(issue, nullptr);
    EXPECT_TRUE(issue->target_candidate_id.empty());
}

TEST(PreflightReportTest, RatingPartsIncompleteWhenEvaluationPartsEmpty) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["evaluation_parts"] = Json::Value(Json::arrayValue);

    const auto report = build_preflight_report(data, base_context());

    ASSERT_TRUE(has_warning_code(report, "rating_parts_incomplete"));
}

TEST(PreflightReportTest, RatingPartsIncompleteAbsentWhenPartsSufficient) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(has_warning_code(report, "rating_parts_incomplete"));
}

// ---------------------------------------------------------------------------
// to_json
// ---------------------------------------------------------------------------

TEST(PreflightReportTest, ToJsonAllGreenStructure) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto report = build_preflight_report(data, base_context());
    const auto json = report.to_json();

    EXPECT_TRUE(json["can_confirm"].asBool());
    EXPECT_FALSE(json["requires_revision_confirmation"].asBool());
    ASSERT_TRUE(json["blocking_errors"].isArray());
    EXPECT_TRUE(json["blocking_errors"].empty());
    ASSERT_TRUE(json["warnings"].isArray());
    EXPECT_TRUE(json["warnings"].empty());
}

TEST(PreflightReportTest, ToJsonBlockingErrorIncludesTargetCandidateId) {
    auto data = valid_data();
    confirm_all_candidates(data);
    // Also revert the linked photo to pending so this scenario yields exactly one
    // blocking issue (candidate_pending_review on the defect); otherwise the confirmed
    // photo's now-unresolved link to the pending defect would add a second issue.
    data["defects"][0]["review_status"] = "待确认";
    data["photos"][0]["review_status"] = "待确认";

    const auto report = build_preflight_report(data, base_context());
    const auto json = report.to_json();

    EXPECT_FALSE(json["can_confirm"].asBool());
    ASSERT_EQ(json["blocking_errors"].size(), 2u);
    bool found_defect_pending = false;
    for (const auto& issue : json["blocking_errors"]) {
        EXPECT_EQ(issue["code"].asString(), "candidate_pending_review");
        EXPECT_TRUE(issue["message"].isString());
        EXPECT_FALSE(issue["message"].asString().empty());
        if (issue["target_candidate_id"].asString() == "defect_0001") {
            found_defect_pending = true;
        }
    }
    EXPECT_TRUE(found_defect_pending);
}

TEST(PreflightReportTest, ToJsonEmitsNullTargetCandidateIdWhenEmpty) {
    auto data = valid_data();
    confirm_all_candidates(data);
    Json::Value structure_parts(Json::arrayValue);
    structure_parts.append(data["ratings"]["structure_parts"][0]);
    data["ratings"]["structure_parts"] = structure_parts;

    const auto report = build_preflight_report(data, base_context());
    const auto json = report.to_json();

    ASSERT_FALSE(json["warnings"].empty());
    bool found_null_target = false;
    for (const auto& warning : json["warnings"]) {
        if (warning["code"].asString() == "rating_parts_incomplete") {
            EXPECT_TRUE(warning["target_candidate_id"].isNull());
            found_null_target = true;
        }
    }
    EXPECT_TRUE(found_null_target);
}

// ---------------------------------------------------------------------------
// Contract 1.2: component rating resolution and independent recalculation
// ---------------------------------------------------------------------------

TEST(PreflightReportTest, PendingComponentRatingBlocksConfirm) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["component_ratings"][0]["review_status"] = "待确认";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    const auto* issue = find_blocking(report, "candidate_pending_review");
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->target_candidate_id, "component_rating_0001");
}

TEST(PreflightReportTest, UnresolvedComponentScoreBlocksConfirm) {
    auto data = valid_data();
    confirm_all_candidates(data);
    auto& rating = data["ratings"]["component_ratings"][0];
    rating["score_validation_status"] = "不一致";
    rating["confirmed_score"] = Json::Value(Json::nullValue);
    rating["score_resolution_reason"] = Json::Value(Json::nullValue);
    // 制造真实的不一致：来源分与复算分两位小数不同。
    rating["source_score"] = 70.0;

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(has_blocking_code(report, "component_score_pending_resolution"));
}

TEST(PreflightReportTest, ManualResolutionWithReasonPassesComponentChecks) {
    auto data = valid_data();
    confirm_all_candidates(data);
    auto& rating = data["ratings"]["component_ratings"][0];
    rating["source_score"] = 70.0;
    rating["score_validation_status"] = "人工采用复算值";
    rating["confirmed_score"] = 65.0;
    rating["score_resolution_reason"] = "现场复核后采用规范复算值。";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_TRUE(report.can_confirm) << [&] {
        std::string all;
        for (const auto& issue : report.blocking_errors) {
            all += issue.code + ": " + issue.message + "\n";
        }
        return all;
    }();
}

TEST(PreflightReportTest, RecalcMismatchBlocksConfirm) {
    auto data = valid_data();
    confirm_all_candidates(data);
    // 篡改 calculated_score：后端独立复算 [35] -> 65，与 60 不符。
    data["ratings"]["component_ratings"][0]["calculated_score"] = 60.0;
    data["ratings"]["component_ratings"][0]["confirmed_score"] = Json::Value(Json::nullValue);
    data["ratings"]["component_ratings"][0]["score_validation_status"] = "人工接受Word值";
    data["ratings"]["component_ratings"][0]["confirmed_score"] = 65.0;
    data["ratings"]["component_ratings"][0]["score_resolution_reason"] = "接受来源分。";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(has_blocking_code(report, "component_score_recalc_mismatch"));
}

TEST(PreflightReportTest, StatusInconsistentWithRecalcBlocksConfirm) {
    auto data = valid_data();
    confirm_all_candidates(data);
    // 状态声称一致，但来源分与复算分（65）两位小数不同。
    data["ratings"]["component_ratings"][0]["source_score"] = 70.0;
    data["ratings"]["component_ratings"][0]["confirmed_score"] = 70.0;

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(has_blocking_code(report, "component_score_recalc_mismatch"));
}

TEST(PreflightReportTest, UncomputableStatusWithCompleteDeductionsBlocksConfirm) {
    auto data = valid_data();
    confirm_all_candidates(data);
    auto& rating = data["ratings"]["component_ratings"][0];
    rating["score_validation_status"] = "无法复算";
    rating["calculated_score"] = Json::Value(Json::nullValue);
    rating["confirmed_score"] = Json::Value(Json::nullValue);
    rating["calculation_details"] = Json::Value(Json::nullValue);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    // 扣分齐全却标无法复算：既提示重新校对，也阻断未解决状态。
    EXPECT_TRUE(has_blocking_code(report, "component_score_recalc_mismatch"));
    EXPECT_TRUE(has_blocking_code(report, "component_score_pending_resolution"));
}

TEST(PreflightReportTest, IgnoredReferencedDefectBlocksConfirm) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["review_status"] = "已忽略";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(has_blocking_code(report, "component_score_defect_reference_invalid"));
}

TEST(PreflightReportTest, UnreferencedDeductionBlocksConfirm) {
    auto data = valid_data();
    confirm_all_candidates(data);
    // 同构件新增一条带扣分的已确认病害，但证据链未纳入。
    Json::Value extra = data["defects"][0];
    extra["candidate_id"] = "defect_0002";
    extra["defect_deduction"] = 20.0;
    extra["photo_numbers"] = Json::Value(Json::arrayValue);
    extra["confirmed_missing_photo_numbers"] = Json::Value(Json::arrayValue);
    data["defects"].append(extra);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    const auto* issue = find_blocking(report, "component_score_deduction_incomplete");
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->target_candidate_id, "component_rating_0001");
}

TEST(PreflightReportTest, DuplicateComponentRatingBlocksConfirm) {
    auto data = valid_data();
    confirm_all_candidates(data);
    Json::Value duplicate = data["ratings"]["component_ratings"][0];
    duplicate["candidate_id"] = "component_rating_0002";
    data["ratings"]["component_ratings"].append(duplicate);

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(has_blocking_code(report, "component_rating_duplicate_component"));
}

TEST(PreflightReportTest, UncomputableAcceptedAsWordValuePassesWithIncompleteDeductions) {
    auto data = valid_data();
    confirm_all_candidates(data);
    // 扣分缺失 -> 无法复算 -> 人工接受Word值：这是合法的处理路径，不得误伤。
    data["defects"][0]["defect_deduction"] = Json::Value(Json::nullValue);
    auto& rating = data["ratings"]["component_ratings"][0];
    rating["calculated_score"] = Json::Value(Json::nullValue);
    rating["calculation_details"] = Json::Value(Json::nullValue);
    rating["score_validation_status"] = "人工接受Word值";
    rating["confirmed_score"] = 65.0;
    rating["score_resolution_reason"] = "扣分列缺失，经复核采信Word来源分。";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_TRUE(report.can_confirm) << [&] {
        std::string all;
        for (const auto& issue : report.blocking_errors) {
            all += issue.code + ": " + issue.message + "\n";
        }
        return all;
    }();
}

TEST(PreflightReportTest, AdoptCalculatedWithoutStoredCalcBlocks) {
    auto data = valid_data();
    confirm_all_candidates(data);
    auto& rating = data["ratings"]["component_ratings"][0];
    rating["calculated_score"] = Json::Value(Json::nullValue);
    rating["calculation_details"] = Json::Value(Json::nullValue);
    rating["score_validation_status"] = "人工采用复算值";
    rating["confirmed_score"] = 65.0;
    rating["score_resolution_reason"] = "采用复算值。";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(has_blocking_code(report, "component_score_recalc_mismatch"));
}

TEST(PreflightReportTest, AcceptWordValueWithComputableEvidenceButNoStoredCalcBlocks) {
    auto data = valid_data();
    confirm_all_candidates(data);
    // 扣分齐全可复算，却仍以"无法复算后接受Word值"的形态提交：证据链已变化，必须重新校对。
    auto& rating = data["ratings"]["component_ratings"][0];
    rating["calculated_score"] = Json::Value(Json::nullValue);
    rating["calculation_details"] = Json::Value(Json::nullValue);
    rating["score_validation_status"] = "人工接受Word值";
    rating["confirmed_score"] = 65.0;
    rating["score_resolution_reason"] = "沿用旧状态。";

    const auto report = build_preflight_report(data, base_context());

    EXPECT_FALSE(report.can_confirm);
    EXPECT_TRUE(has_blocking_code(report, "component_score_recalc_mismatch"));
}
