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

/// 某个构件上某种病害类型在两个年度各有多少条。写成"横向裂缝 2 条"要靠它。
struct WorkspaceDefectTypeDelta {
    std::string defect_type;
    int previous_count{0};
    int latest_count{0};

    Json::Value to_json() const;
};

/**
 * @brief 一类构件（结构分部 + 构件类型）在相邻两个年度的病害情况。
 *
 * 按类型汇总而不是逐构件成段：一座桥几百个构件，逐个写会得到几十段几乎相同的话；
 * 报告里本来也是按部位/类型描述的。
 *
 * 条数统计**含该类型下的全部构件**，包括与上年持平的那些——只统计有变化的构件会让
 * "桥面铺装 2026 年记录 N 条"这句话本身就是错的。
 */
struct WorkspaceDefectGroupDelta {
    std::string structure_part;
    std::string component_type;
    int previous_count{0};
    int latest_count{0};
    /// 该类型下两年中出现过病害的构件数，以及其中条数有变化的构件数。
    int component_count{0};
    int changed_component_count{0};
    /// 按最新年度条数降序；两年都出现过的类型只占一项。
    std::vector<WorkspaceDefectTypeDelta> defect_types;

    Json::Value to_json() const;
};

/**
 * @brief 最新年度与上一年度的病害对比。
 *
 * 统计的是**条数**，不是病害身份的匹配。跨年身份要靠 defect_threads，而未整理的观测
 * 本来就没有线索；`defect_comparisons` 又是模块 07 的预留面，当前为空。所以这里只回答
 * "这个构件去年几条、今年几条"，增减是各构件差值的汇总，不声称哪一条对应哪一条——
 * 界面文案也必须照这个口径写，不能说成"新增了 N 处病害"。
 */
struct WorkspaceDefectComparison {
    /// 当前有效且已确认的年度不足两个时为 false，界面据此不渲染对比区。
    bool available{false};
    int previous_year{0};
    int latest_year{0};
    int previous_observation_count{0};
    int latest_observation_count{0};
    /// 各构件正差值之和 / 负差值绝对值之和。两者不会互相抵消。
    int increased_observation_count{0};
    int decreased_observation_count{0};
    int changed_component_count{0};
    int unchanged_component_count{0};
    /// 按构件类型汇总，按最新年度条数降序。
    std::vector<WorkspaceDefectGroupDelta> groups;

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
    WorkspaceDefectComparison defect_comparison;

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
