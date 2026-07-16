#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace bridge_report::deletion {

struct DeletionCounts {
    int inspection_versions{0};
    int import_records{0};
    int defect_observations{0};
    int defect_measurements{0};
    int defect_photos{0};
    int condition_ratings{0};
    int archived_files_to_delete{0};
    int temporary_source_files_to_delete{0};
    int shared_files_retained{0};
    int defect_threads_affected{0};
    int defect_comparisons{0};

    Json::Value to_json() const;
};

struct ActiveDeletionLock {
    std::string import_record_id;
    std::string owner_username;
    std::string owner_display_name;
    std::string acquired_at;
    std::string expires_at;

    Json::Value to_json() const;
};

struct InspectionYearDeletionPlan {
    std::string bridge_id;
    std::string bridge_system_number;
    std::string bridge_name;
    int inspection_year{0};
    std::vector<int> version_numbers;
    DeletionCounts counts;
    std::vector<ActiveDeletionLock> active_edit_locks;

    // 仅供仓储层执行和并发指纹使用，绝不进入 API JSON。
    std::vector<std::string> inspection_year_ids;
    std::vector<std::string> import_record_ids;
    std::vector<std::string> defect_observation_ids;
    std::vector<std::string> condition_rating_ids;
    std::vector<std::string> defect_thread_ids;
    std::vector<std::string> defect_comparison_ids;
    std::vector<std::string> archived_file_ids_to_delete;
    std::vector<std::string> archived_file_relative_paths_to_delete;
    std::vector<std::string> temporary_source_file_ids_to_delete;
    std::vector<std::string> temporary_source_relative_paths_to_delete;
    std::vector<std::string> fingerprint_items;

    std::string confirmation_text() const;
    std::string impact_token() const;
    Json::Value to_public_json() const;
};

struct DeletionActorSnapshot {
    std::string user_id;
    std::string username;
    std::string display_name;
};

enum class DeleteInspectionYearStatus {
    Deleted,
    NotFound,
    Locked,
    ImpactChanged,
    Failed,
};

struct DeleteInspectionYearOutcome {
    DeleteInspectionYearStatus status{DeleteInspectionYearStatus::Failed};
    std::optional<InspectionYearDeletionPlan> current_plan;
    std::optional<std::string> deletion_audit_id;
    std::optional<std::string> next_inspection_year_id;
};

}  // namespace bridge_report::deletion
