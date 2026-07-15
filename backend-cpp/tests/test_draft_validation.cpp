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

// ---------------------------------------------------------------------------
// 重开校对（warnings_only）范围校验与警告病害判定。
// ---------------------------------------------------------------------------

namespace {

// 极简病害候选：只含范围校验关心的字段（candidate_id / warnings / 任意业务字段）。
Json::Value make_defect(const std::string& candidate_id, bool with_warning, double deduction) {
    Json::Value defect;
    defect["candidate_id"] = candidate_id;
    defect["defect_deduction"] = deduction;
    defect["warnings"] = Json::Value(Json::arrayValue);
    if (with_warning) {
        Json::Value warning;
        warning["code"] = "scale_not_positive_integer";
        warning["message"] = "病害标度不是正整数";
        warning["severity"] = "warning";
        defect["warnings"].append(warning);
    }
    return defect;
}

Json::Value make_draft(std::initializer_list<Json::Value> defects) {
    Json::Value draft;
    draft["defects"] = Json::Value(Json::arrayValue);
    for (const auto& defect : defects) {
        draft["defects"].append(defect);
    }
    return draft;
}

void add_warning(Json::Value& defect) {
    Json::Value warning;
    warning["code"] = "needs_review";
    warning["message"] = "需要人工复核";
    warning["severity"] = "warning";
    defect["warnings"].append(warning);
}

}  // namespace

TEST(DraftHasWarningDefectsTest, DetectsWarningDefects) {
    EXPECT_TRUE(bridge_report::review::draft_has_warning_defects(
        make_draft({make_defect("defect_0001", true, 35.0)})));
    EXPECT_FALSE(bridge_report::review::draft_has_warning_defects(
        make_draft({make_defect("defect_0001", false, 35.0)})));
    EXPECT_FALSE(bridge_report::review::draft_has_warning_defects(Json::Value(Json::objectValue)));
}

TEST(WarningsOnlyScopeTest, AllowsEditingWarningDefects) {
    const auto stored = make_draft({make_defect("defect_0001", true, 35.0), make_defect("defect_0002", false, 20.0)});
    auto next = make_draft({make_defect("defect_0001", true, 40.0), make_defect("defect_0002", false, 20.0)});

    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next);

    EXPECT_TRUE(result.ok) << result.message;
}

TEST(WarningsOnlyScopeTest, RejectsEditingDefectsWithoutWarnings) {
    const auto stored = make_draft({make_defect("defect_0001", true, 35.0), make_defect("defect_0002", false, 20.0)});
    const auto next = make_draft({make_defect("defect_0001", true, 35.0), make_defect("defect_0002", false, 25.0)});

    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "reopen_scope_violation");
    ASSERT_EQ(result.issues.size(), 1u);
    EXPECT_NE(result.issues[0].message.find("defect_0002"), std::string::npos);
}

TEST(WarningsOnlyScopeTest, RejectsAddingOrRemovingDefects) {
    const auto stored = make_draft({make_defect("defect_0001", true, 35.0), make_defect("defect_0002", false, 20.0)});
    const auto removed = make_draft({make_defect("defect_0001", true, 35.0)});
    const auto added = make_draft({
        make_defect("defect_0001", true, 35.0),
        make_defect("defect_0002", false, 20.0),
        make_defect("defect_0003", false, 10.0),
    });

    EXPECT_FALSE(bridge_report::review::validate_warnings_only_scope(stored, removed).ok);
    EXPECT_FALSE(bridge_report::review::validate_warnings_only_scope(stored, added).ok);
}

TEST(WarningsOnlyScopeTest, TreatsIntegralRealAndIntAsEqual) {
    // jsonb -> 前端 JSON 往返会把 20.0 变成 20；未改动的锁定病害不得因此误报。
    auto stored = make_draft({make_defect("defect_0001", true, 35.0), make_defect("defect_0002", false, 20.0)});
    auto next = make_draft({make_defect("defect_0001", true, 35.0), make_defect("defect_0002", false, 20.0)});
    next["defects"][1]["defect_deduction"] = 20;  // int 表示

    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next);

    EXPECT_TRUE(result.ok) << result.message;
}

TEST(WarningsOnlyScopeTest, ClientCannotFakeWarningsToUnlockDefects) {
    // 是否带警告只看存量草稿：客户端在请求体里给锁定病害补 warnings 数组不解锁。
    const auto stored = make_draft({make_defect("defect_0001", true, 35.0), make_defect("defect_0002", false, 20.0)});
    auto next = make_draft({make_defect("defect_0001", true, 35.0), make_defect("defect_0002", true, 25.0)});

    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next);

    EXPECT_FALSE(result.ok);
}

TEST(WarningsOnlyScopeTest, AllowsOnlyWhitelistedFieldsOnWarningDefect) {
    auto stored = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    add_warning(stored["defects"][0]);
    auto next = stored;
    next["defects"][0]["defect_location"] = "0#台顶处、小桩号立面左侧端部";
    next["defects"][0]["review_status"] = "已修改";

    Json::Value normalized;
    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next, &normalized);

    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(normalized["defects"][0]["defect_location"].asString(), "0#台顶处、小桩号立面左侧端部");
}

TEST(WarningsOnlyScopeTest, RejectsChangingEvidenceOnWarningDefect) {
    auto stored = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    add_warning(stored["defects"][0]);
    auto next = stored;
    next["defects"][0]["source_ref"]["raw_row_text"] = "伪造来源证据";

    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "reopen_scope_violation");
}

TEST(WarningsOnlyScopeTest, RejectsChangingPhotosOrOtherRatingLevels) {
    auto stored = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    add_warning(stored["defects"][0]);

    auto photo_changed = stored;
    photo_changed["photos"][0]["photo_number"] = "2.1-99";
    EXPECT_FALSE(bridge_report::review::validate_warnings_only_scope(stored, photo_changed).ok);

    auto overall_changed = stored;
    overall_changed["ratings"]["overall"]["total_score"] = 99.0;
    EXPECT_FALSE(bridge_report::review::validate_warnings_only_scope(stored, overall_changed).ok);

    auto inspection_changed = stored;
    inspection_changed["inspection"]["inspection_year"] = 2030;
    EXPECT_FALSE(bridge_report::review::validate_warnings_only_scope(stored, inspection_changed).ok);
}

TEST(WarningsOnlyScopeTest, AcceptsOnlyBackendDerivedComponentRatingAfterDeductionChange) {
    auto stored = read_contract_fixture("bridge_annual_inspection_data.valid.json");
    add_warning(stored["defects"][0]);
    auto next = stored;
    next["defects"][0]["defect_deduction"] = 20.0;
    auto& rating = next["ratings"]["component_ratings"][0];
    rating["calculated_score"] = 80.0;
    rating["calculation_details"]["ordered_deductions"] = Json::Value(Json::arrayValue);
    rating["calculation_details"]["ordered_deductions"].append(20.0);
    rating["score_validation_status"] = "不一致";
    rating["confirmed_score"] = Json::Value(Json::nullValue);
    rating["score_resolution_reason"] = Json::Value(Json::nullValue);
    rating["review_status"] = "待确认";

    Json::Value normalized;
    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next, &normalized);

    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_EQ(normalized["ratings"]["component_ratings"][0]["calculated_score"].asDouble(), 80.0);

    auto forged = next;
    forged["ratings"]["component_ratings"][0]["confirmed_score"] = 99.0;
    forged["ratings"]["component_ratings"][0]["score_validation_status"] = "人工接受Word值";
    forged["ratings"]["component_ratings"][0]["score_resolution_reason"] = "越权修改";
    EXPECT_FALSE(bridge_report::review::validate_warnings_only_scope(stored, forged).ok);
}
