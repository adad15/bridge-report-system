#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

#include "bridge_report/deletion/InspectionYearDeletionModels.hpp"

namespace bridge_report::deletion {

struct ImportRecordDeletionCounts {
    int defects{0};
    int photos{0};
    int rating_items{0};
    int parsed_images{0};
    int archived_files_to_delete{0};
    int temporary_word_files_to_delete{0};
    int parse_work_directories_to_delete{0};
    int shared_files_retained{0};
    int formal_fact_references{0};

    Json::Value to_json() const;
};

struct ImportRecordDeletionPlan {
    std::string import_record_id;
    std::string import_system_number;
    std::string import_name;
    std::string import_status;
    std::string source_type;
    std::string updated_at;
    std::string bridge_id;
    std::string bridge_system_number;
    std::string bridge_name;
    std::string inspection_year_id;
    int inspection_year{0};
    int inspection_version{0};
    ImportRecordDeletionCounts counts;
    std::vector<ActiveDeletionLock> active_edit_locks;
    std::vector<std::string> archived_file_ids_to_delete;
    std::vector<std::string> archived_file_relative_paths_to_delete;
    std::vector<std::string> temporary_source_ids_to_delete;
    std::vector<std::string> temporary_source_relative_paths_to_delete;
    std::vector<std::string> parse_work_relative_paths_to_delete;
    std::vector<std::string> fingerprint_items;

    bool status_allows_delete() const;
    bool can_delete() const;
    std::optional<std::string> block_code() const;
    std::string confirmation_text() const;
    std::string impact_token() const;
    Json::Value to_public_json() const;
};

enum class DeleteImportRecordStatus {
    Deleted,
    NotFound,
    NotDeletable,
    Locked,
    HasFormalFacts,
    ImpactChanged,
    Failed,
};

struct DeleteImportRecordOutcome {
    DeleteImportRecordStatus status{DeleteImportRecordStatus::Failed};
    std::optional<ImportRecordDeletionPlan> current_plan;
    std::optional<std::string> deletion_audit_id;
};

}  // namespace bridge_report::deletion
