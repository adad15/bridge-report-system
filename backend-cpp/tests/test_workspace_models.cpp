#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/review/WorkspaceModels.hpp"

using bridge_report::review::BridgeOverview;
using bridge_report::review::InspectionWorkspace;
using bridge_report::review::WorkspaceBridge;
using bridge_report::review::WorkspaceDefectArchiveSummary;
using bridge_report::review::WorkspaceEditLock;
using bridge_report::review::WorkspaceImport;
using bridge_report::review::WorkspaceImportAction;
using bridge_report::review::WorkspaceInspection;
using bridge_report::review::WorkspacePendingSummary;
using bridge_report::review::WorkspaceStructureRating;
using bridge_report::review::derive_workspace_import_action;
using bridge_report::review::workspace_import_action_name;

namespace {

WorkspaceBridge sample_bridge() {
    return WorkspaceBridge{
        "b1111111-1111-1111-1111-111111111111",
        "QL-000001",
        "绕阳河二号桥",
        "G305",
        "在用"
    };
}

WorkspaceInspection sample_inspection() {
    WorkspaceInspection inspection;
    inspection.id = "y1111111-1111-1111-1111-111111111111";
    inspection.system_number = "NDJC-000001";
    inspection.inspection_year = 2026;
    inspection.status = "已确认";
    inspection.version_number = 1;
    inspection.is_current = true;
    inspection.overall_score = 85.61;
    inspection.overall_grade = "2类";
    inspection.created_at = "2026-07-12 08:28:00+08";
    inspection.updated_at = "2026-07-15 14:22:00+08";
    return inspection;
}

}  // namespace

TEST(WorkspaceImportActionTest, DerivesStableActionsFromExistingStatuses) {
    EXPECT_EQ(derive_workspace_import_action("已上传"), WorkspaceImportAction::Parse);
    EXPECT_EQ(derive_workspace_import_action("解析失败"), WorkspaceImportAction::Parse);
    EXPECT_EQ(derive_workspace_import_action("待校对"), WorkspaceImportAction::ContinueReview);
    EXPECT_EQ(derive_workspace_import_action("已确认"), WorkspaceImportAction::ViewResult);
    EXPECT_EQ(derive_workspace_import_action("已取消"), WorkspaceImportAction::ViewResult);
    EXPECT_EQ(derive_workspace_import_action("解析中"), WorkspaceImportAction::None);
    EXPECT_EQ(derive_workspace_import_action("解析失败", "已过期"), WorkspaceImportAction::Reupload);
    EXPECT_EQ(derive_workspace_import_action("未知状态"), WorkspaceImportAction::None);

    EXPECT_EQ(workspace_import_action_name(WorkspaceImportAction::Parse), "parse");
    EXPECT_EQ(workspace_import_action_name(WorkspaceImportAction::ContinueReview), "continue_review");
    EXPECT_EQ(workspace_import_action_name(WorkspaceImportAction::ViewResult), "view_result");
    EXPECT_EQ(workspace_import_action_name(WorkspaceImportAction::Reupload), "reupload");
    EXPECT_EQ(workspace_import_action_name(WorkspaceImportAction::None), "none");
}

TEST(BridgeOverviewTest, EmitsNullLatestInspectionAndEmptyCollectionsForEmptyArchive) {
    BridgeOverview overview;
    overview.bridge = sample_bridge();
    overview.pending = WorkspacePendingSummary{};
    overview.defect_archive = WorkspaceDefectArchiveSummary{};

    const auto json = overview.to_json();

    EXPECT_EQ(json["bridge"]["bridge_name"].asString(), "绕阳河二号桥");
    EXPECT_TRUE(json["latest_inspection"].isNull());
    EXPECT_TRUE(json["recent_inspections"].isArray());
    EXPECT_TRUE(json["recent_inspections"].empty());
    EXPECT_TRUE(json["structure_ratings"].isArray());
    EXPECT_EQ(json["pending"]["total_count"].asInt(), 0);
    EXPECT_EQ(json["defect_archive"]["component_count"].asInt(), 0);
}

TEST(BridgeOverviewTest, EmitsFormalInspectionRatingsPendingAndArchiveSummary) {
    BridgeOverview overview;
    overview.bridge = sample_bridge();
    overview.latest_inspection = sample_inspection();
    overview.recent_inspections.push_back(sample_inspection());
    overview.structure_ratings.push_back(WorkspaceStructureRating{"结构分部", "上部结构", 87.45, "2类"});
    overview.pending = WorkspacePendingSummary{2, 7};
    overview.defect_archive = WorkspaceDefectArchiveSummary{18, 25, 7};

    const auto json = overview.to_json();

    EXPECT_DOUBLE_EQ(json["latest_inspection"]["overall_score"].asDouble(), 85.61);
    EXPECT_EQ(json["latest_inspection"]["overall_grade"].asString(), "2类");
    EXPECT_EQ(json["recent_inspections"].size(), 1u);
    EXPECT_EQ(json["structure_ratings"][0]["rating_item_name"].asString(), "上部结构");
    EXPECT_DOUBLE_EQ(json["structure_ratings"][0]["score"].asDouble(), 87.45);
    EXPECT_EQ(json["pending"]["total_count"].asInt(), 9);
    EXPECT_EQ(json["defect_archive"]["thread_count"].asInt(), 25);
}

TEST(WorkspaceImportTest, EmitsReviewStatisticsActionAndOptionalLock) {
    WorkspaceImport item;
    item.id = "i1111111-1111-1111-1111-111111111111";
    item.system_number = "DRJL-001290";
    item.import_name = "绕阳河二号桥报告.docx";
    item.source_type = "软件导出Word";
    item.import_status = "待校对";
    item.importer_name = "张工";
    item.created_at = "2026-07-12 08:28:00+08";
    item.updated_at = "2026-07-15 14:22:00+08";
    item.error_message = "未识别到表4.1-2总体技术状况评定表。";
    item.statistics.defect_count = 25;
    item.statistics.photo_count = 31;
    item.statistics.rating_item_count = 15;
    item.statistics.pending_count = 6;
    item.edit_lock = WorkspaceEditLock{"zhang", "张工", "2026-07-15 14:20:00+08", "2026-07-15 14:22:00+08"};

    const auto json = item.to_json();

    EXPECT_EQ(json["available_action"].asString(), "continue_review");
    EXPECT_EQ(json["statistics"]["defect_count"].asInt(), 25);
    EXPECT_EQ(json["statistics"]["photo_count"].asInt(), 31);
    EXPECT_EQ(json["statistics"]["rating_item_count"].asInt(), 15);
    EXPECT_EQ(json["edit_lock"]["owner_display_name"].asString(), "张工");
    EXPECT_EQ(json["error_message"].asString(), "未识别到表4.1-2总体技术状况评定表。");

    item.edit_lock = std::nullopt;
    EXPECT_TRUE(item.to_json()["edit_lock"].isNull());
}

TEST(InspectionWorkspaceTest, EmitsBridgeYearImportsAndPendingSummary) {
    InspectionWorkspace workspace;
    workspace.bridge = sample_bridge();
    workspace.inspection_year = sample_inspection();
    workspace.pending = WorkspacePendingSummary{1, 7};

    WorkspaceImport item;
    item.id = "i1111111-1111-1111-1111-111111111111";
    item.system_number = "DRJL-001290";
    item.import_name = "绕阳河二号桥报告.docx";
    item.source_type = "软件导出Word";
    item.import_status = "已上传";
    workspace.imports.push_back(item);

    const auto json = workspace.to_json();

    EXPECT_EQ(json["bridge"]["id"].asString(), "b1111111-1111-1111-1111-111111111111");
    EXPECT_EQ(json["inspection_year"]["inspection_year"].asInt(), 2026);
    ASSERT_EQ(json["imports"].size(), 1u);
    EXPECT_EQ(json["imports"][0]["available_action"].asString(), "parse");
    EXPECT_EQ(json["pending"]["total_count"].asInt(), 8);
}
