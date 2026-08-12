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
        "samples/contracts/bridge_annual_inspection_data.v4.valid.json";
    std::ifstream input(path, std::ios::binary);
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!input || !Json::parseFromStream(builder, input, &root, &errors)) {
        throw std::runtime_error("unable to load v4 fixture: " + errors);
    }
    return root;
}

bridge_report::rating_tree::EffectiveRatingTree rating_tree_fixture() {
    using namespace bridge_report::rating_tree;
    EffectiveRatingTree tree;
    EffectiveRatingTreeNode crack;
    crack.id = "11111111-1111-4111-8111-111111111111";
    crack.display_name = "裂缝";
    crack.node_type = RatingTreeNodeType::defect;
    crack.bridge_type_ids = {"beam"};
    crack.component_category_ids = {"main_girder"};
    crack.scoring_mode = RatingTreeScoringMode::inherit_h21;
    crack.h21_indicator_id = "h21.crack";
    crack.is_selectable = true;
    crack.is_scoring = true;
    crack.allowed_scales = {1, 2, 3, 4, 5};
    crack.source_mappings.push_back({
        "source-group-crack",
        "source-indicator-crack",
        "5.1.1",
        "5.1.1-1",
        crack.id});
    tree.nodes.emplace(crack.id, crack);

    EffectiveRatingTreeNode water;
    water.id = "22222222-2222-4222-8222-222222222222";
    water.display_name = "水损";
    water.node_type = RatingTreeNodeType::defect;
    water.bridge_type_ids = {"beam"};
    water.component_category_ids = {"main_girder"};
    water.scoring_mode = RatingTreeScoringMode::non_scoring;
    water.is_selectable = true;
    water.is_scoring = false;
    tree.nodes.emplace(water.id, water);
    return tree;
}

bridge_report::inventory::InventoryRevision inventory_fixture(
    std::string category = "main_girder") {
    bridge_report::inventory::InventoryRevision revision;
    revision.id = "revision-1";
    revision.status = "已确认";
    bridge_report::inventory::InventoryEntry entry;
    entry.bridge_component_id = "component-1";
    entry.is_active = true;
    bridge_report::inventory::InventoryMapping mapping;
    mapping.standard_package_id = "h21-package";
    mapping.standard_bridge_type_id = "beam";
    mapping.standard_component_category_id = std::move(category);
    mapping.confirmation_status = "已确认";
    mapping.is_active = true;
    entry.mappings.push_back(std::move(mapping));
    revision.entries.push_back(std::move(entry));
    return revision;
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
    next["photos"][0]["extracted_file"]["original_caption"] = "被修改的图注";
    const auto result = bridge_report::review::validate_warnings_only_scope(stored, next);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "reopen_scope_violation");
}

TEST(DraftValidationTest, AuditIsNullWhenDefectSetIsUnchanged) {
    const auto data = fixture();
    EXPECT_TRUE(
        bridge_report::review::build_defect_change_audit_event(data, data, "editor").isNull());
}

TEST(DraftValidationTest, AcceptsUnchangedImportedDefectEvidence) {
    const auto data = fixture();
    const auto result =
        bridge_report::review::validate_imported_defect_evidence(data, data);

    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.code.empty());
}

TEST(DraftValidationTest, RejectsChangedImportedDefectEvidence) {
    const auto stored = fixture();
    auto draft = stored;
    draft["defects"][0]["source_ref"]["raw_row_text"] = "被修改的来源证据";

    const auto result =
        bridge_report::review::validate_imported_defect_evidence(stored, draft);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, "imported_evidence_modified");
    ASSERT_EQ(result.issues.size(), 1U);
}

TEST(DraftValidationTest, SourceMappingDerivesH21AndIgnoresForgery) {
    auto stored = fixture();
    auto draft = stored;
    auto& defect = draft["defects"][0];
    defect["bridge_component_id"] = "component-1";
    defect["standard_defect_indicator_id"] = "forged";
    defect["source_defect_group_id"] = "source-group-crack";
    defect["source_defect_indicator_id"] = "source-indicator-crack";
    defect["source_defect_group_number"] = "5.1.1";
    defect["source_defect_indicator_number"] = "5.1.1-1";

    const auto result =
        bridge_report::review::normalize_defect_rating_tree_associations(
            draft,
            stored,
            "tree-version-1",
            "h21-package",
            rating_tree_fixture(),
            inventory_fixture());

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(
        defect["rating_tree_node_id"].asString(),
        "11111111-1111-4111-8111-111111111111");
    EXPECT_EQ(defect["standard_defect_indicator_id"].asString(), "h21.crack");
    EXPECT_EQ(defect["rating_tree_match_method"].asString(), "source_indicator");
}

TEST(DraftValidationTest, RatingTreeNormalizationRejectsInapplicableManualNode) {
    auto stored = fixture();
    stored["defects"][0]["bridge_component_id"] = "component-1";
    auto draft = stored;
    draft["defects"][0]["rating_tree_node_id"] =
        "11111111-1111-4111-8111-111111111111";

    const auto result =
        bridge_report::review::normalize_defect_rating_tree_associations(
            draft,
            stored,
            "tree-version-1",
            "h21-package",
            rating_tree_fixture(),
            inventory_fixture("deck"));

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(draft["defects"][0]["rating_tree_node_id"].isNull());
    EXPECT_TRUE(draft["defects"][0]["standard_defect_indicator_id"].isNull());
}

TEST(DraftValidationTest, RatingTreeConfirmationValidatesScaleAndAllowsNonScoring) {
    auto data = fixture();
    auto& defect = data["defects"][0];
    defect["bridge_component_id"] = "component-1";
    defect["review_status"] = "已确认";
    defect["group_review_status"] = "已确认";
    defect["rating_tree_version_id"] = "tree-version-1";
    defect["rating_tree_node_id"] =
        "11111111-1111-4111-8111-111111111111";
    defect["standard_defect_indicator_id"] = "h21.crack";
    defect["defect_scale"] = 9;

    auto result =
        bridge_report::review::validate_defect_rating_tree_for_confirmation(
            data,
            "tree-version-1",
            "h21-package",
            rating_tree_fixture(),
            inventory_fixture());
    EXPECT_FALSE(result.ok);

    defect["rating_tree_node_id"] =
        "22222222-2222-4222-8222-222222222222";
    defect["standard_defect_indicator_id"] = Json::Value();
    defect["defect_scale"] = Json::Value();
    result =
        bridge_report::review::validate_defect_rating_tree_for_confirmation(
            data,
            "tree-version-1",
            "h21-package",
            rating_tree_fixture(),
            inventory_fixture());
    EXPECT_TRUE(result.ok) << result.message;
}
