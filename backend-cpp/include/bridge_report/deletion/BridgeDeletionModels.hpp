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
    int bridge_components{0};
    int component_aliases{0};
    int defect_threads{0};
    int defect_observations{0};
    int defect_measurements{0};
    int defect_photos{0};
    int condition_ratings{0};
    int defect_comparisons{0};
    int archived_files_to_delete{0};
    int shared_files_retained{0};

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
    std::vector<std::string> fingerprint_items;

    std::string impact_token() const;
    Json::Value to_public_json() const;
};

std::string bridge_deletion_confirmation_text(std::vector<std::string> system_numbers);

enum class DeleteBridgeStatus { Deleted, NotFound, Locked, ImpactChanged, Failed };

struct DeleteBridgeOutcome {
    DeleteBridgeStatus status{DeleteBridgeStatus::Failed};
    std::optional<BridgeDeletionPlan> current_plan;
    std::optional<std::string> deletion_audit_id;
};

}  // namespace bridge_report::deletion
