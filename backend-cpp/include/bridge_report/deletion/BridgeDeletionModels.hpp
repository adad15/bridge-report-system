#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

#include "bridge_report/deletion/InspectionYearDeletionModels.hpp"

namespace bridge_report::deletion {

struct BridgeDeletionCounts {
    int inspection_years{0};
    int inspection_versions{0};
    int import_records{0};
    int bridge_aliases{0};
    /// 桥梁图件：地理位置图、桥型布置图与桥梁照片，文件本身算在归档文件里。
    int bridge_media{0};
    int bridge_components{0};
    int component_aliases{0};
    int component_generation_batches{0};
    int component_inventory_revisions{0};
    int component_inventory_entries{0};
    int component_standard_mappings{0};
    int defect_threads{0};
    int defect_observations{0};
    int defect_measurements{0};
    int defect_photos{0};
    int condition_ratings{0};
    int defect_comparisons{0};
    // 见 InspectionYearDeletionModels.hpp 里同名字段的说明。
    int assessment_runs{0};
    int formal_assessment_runs{0};
    int archived_files_to_delete{0};
    int temporary_source_files_to_delete{0};
    int shared_files_retained{0};
    // 报告配置与生成任务，见 InspectionYearDeletionModels.hpp 里同名字段的说明。
    int report_settings{0};
    int report_personnel_assignments{0};
    int report_equipment_assignments{0};
    int report_generation_jobs{0};
    int running_report_generation_jobs{0};
    int report_comparison_references{0};

    Json::Value to_json() const;
    BridgeDeletionCounts& operator+=(const BridgeDeletionCounts& other);
};

struct BridgeDeletionPlan {
    std::string bridge_id;
    std::string bridge_system_number;
    std::string bridge_name;
    std::optional<std::string> route_number;
    std::optional<std::string> route_name;
    std::optional<std::string> station_mark;
    std::string status;
    BridgeDeletionCounts counts;
    std::vector<ActiveDeletionLock> active_edit_locks;

    std::vector<std::string> archived_file_ids_to_delete;
    std::vector<std::string> archived_file_relative_paths_to_delete;
    std::vector<std::string> temporary_source_file_ids_to_delete;
    std::vector<std::string> temporary_source_relative_paths_to_delete;
    std::vector<std::string> assessment_run_ids;
    std::vector<std::string> fingerprint_items;

    std::string impact_token() const;
    Json::Value to_public_json() const;
};

std::string bridge_deletion_confirmation_text(std::vector<std::string> system_numbers);

enum class DeleteBridgeStatus {
    Deleted,
    NotFound,
    Locked,
    ImpactChanged,
    /// 该桥存在已完成的正式评定，见 DeleteInspectionYearStatus 同名值。
    FormalAssessmentPresent,
    /// 该桥有正在运行的报告生成任务，见 DeleteInspectionYearStatus 同名值。
    ReportGenerationJobRunning,
    Failed,
};

struct DeleteBridgeOutcome {
    DeleteBridgeStatus status{DeleteBridgeStatus::Failed};
    std::optional<BridgeDeletionPlan> current_plan;
    std::optional<std::string> deletion_audit_id;
};

}  // namespace bridge_report::deletion
