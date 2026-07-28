#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json/value.h>

#include "bridge_report/review/ReviewStatistics.hpp"

namespace bridge_report::review {

enum class WorkspaceImportAction {
    Parse,
    ContinueReview,
    ViewResult,
    Reupload,
    None,
};

WorkspaceImportAction derive_workspace_import_action(
    std::string_view import_status,
    std::string_view temporary_source_status = {}
);
std::string_view workspace_import_action_name(WorkspaceImportAction action);

struct WorkspaceBridge {
    std::string id;
    std::string system_number;
    std::string bridge_name;
    std::optional<std::string> route_name;
    std::string status;

    Json::Value to_json() const;
};

struct WorkspaceInspection {
    std::string id;
    std::string system_number;
    int inspection_year{0};
    std::string status;
    int version_number{0};
    bool is_current{false};
    std::optional<double> overall_score;
    std::optional<std::string> overall_grade;
    std::optional<std::string> created_at;
    std::optional<std::string> updated_at;

    Json::Value to_json() const;
};

struct WorkspaceStandardPackage {
    std::string id;
    std::string family;
    std::string standard_code;
    std::string standard_name;
    std::string official_edition;
    std::string package_version;
    bool is_enabled{false};
    std::string sync_status;

    Json::Value to_json() const;
};

struct WorkspaceStandardProfile {
    std::string id;
    int revision_number{0};
    std::string status;
    WorkspaceStandardPackage technical_condition;
    WorkspaceStandardPackage maintenance;
    std::string rating_tree_version_id;
    std::string rating_tree_name;
    std::string rating_tree_package_version;
    std::string rating_tree_content_checksum;

    Json::Value to_json() const;
};

struct WorkspaceStructureRating {
    std::string rating_level;
    std::string rating_item_name;
    std::optional<double> score;
    std::optional<std::string> grade;

    Json::Value to_json() const;
};

struct WorkspacePendingSummary {
    int import_count{0};
    int unbound_observation_count{0};

    int total_count() const;
    Json::Value to_json() const;
};

struct WorkspaceDefectArchiveSummary {
    int component_count{0};
    int thread_count{0};
    int unbound_observation_count{0};

    Json::Value to_json() const;
};

struct WorkspaceEditLock {
    std::string owner_username;
    std::string owner_display_name;
    std::string acquired_at;
    std::string expires_at;

    Json::Value to_json() const;
};

struct WorkspaceImport {
    std::string id;
    std::string system_number;
    std::string import_name;
    std::string source_type;
    std::string import_status;
    std::optional<std::string> importer_name;
    std::optional<std::string> created_at;
    std::optional<std::string> updated_at;
    std::optional<std::string> error_message;
    std::optional<std::string> temporary_source_status;
    std::optional<std::string> temporary_source_expires_at;
    ReviewStatistics statistics;
    std::optional<WorkspaceEditLock> edit_lock;

    Json::Value to_json() const;
};

struct BridgeOverview {
    WorkspaceBridge bridge;
    std::optional<WorkspaceInspection> latest_inspection;
    std::vector<WorkspaceInspection> recent_inspections;
    std::vector<WorkspaceStructureRating> structure_ratings;
    WorkspacePendingSummary pending;
    WorkspaceDefectArchiveSummary defect_archive;

    Json::Value to_json() const;
};

struct InspectionWorkspace {
    WorkspaceBridge bridge;
    WorkspaceInspection inspection_year;
    std::optional<WorkspaceStandardProfile> standard_profile;
    std::vector<WorkspaceImport> imports;
    WorkspacePendingSummary pending;

    Json::Value to_json() const;
};

}  // namespace bridge_report::review
