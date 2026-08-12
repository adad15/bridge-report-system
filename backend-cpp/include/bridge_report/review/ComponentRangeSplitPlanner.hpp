#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <json/json.h>

#include "bridge_report/inventory/ComponentInventoryModels.hpp"

namespace bridge_report::review {

struct ComponentRangeSplitTarget {
    std::string part_name;
    std::string component_number;
};

struct ComponentRangeSplitItem {
    ComponentRangeSplitTarget target;
    int expanded_component_count{0};
    int source_defect_count{0};
    int result_defect_count{0};
    int result_photo_count{0};
    int bound_count{0};
    int ambiguous_count{0};
    int unmatched_count{0};
};

struct ComponentRangeSplitTotals {
    int selected_range_count{0};
    int source_defect_count{0};
    int result_defect_count{0};
    int result_photo_count{0};
    int bound_count{0};
    int ambiguous_count{0};
    int unmatched_count{0};
};

struct ComponentRangeSplitMatch {
    std::string component_number;
    std::string bridge_component_id;
    std::string standard_component_category_id;
    std::string resolved_structure_part;
    std::string match_method;
    std::vector<std::string> candidate_component_ids;
};

struct ComponentRangeSplitWorkItem {
    ComponentRangeSplitItem summary;
    std::vector<std::string> source_candidate_ids;
    std::vector<ComponentRangeSplitMatch> matches;
};

enum class ComponentRangeSplitPlanStatus {
    Ok,
    InvalidTarget,
    IneligibleTarget,
    RangeLimitExceeded,
    ResultLimitExceeded,
};

struct ComponentRangeSplitPlan {
    ComponentRangeSplitPlanStatus status{ComponentRangeSplitPlanStatus::Ok};
    std::string error_code;
    std::string error_message;
    ComponentRangeSplitTarget rejected_target;
    std::vector<ComponentRangeSplitItem> items;
    ComponentRangeSplitTotals totals;
    Json::Value result_json;
};

struct ComponentRangeSplitAnalysis {
    ComponentRangeSplitPlanStatus status{ComponentRangeSplitPlanStatus::Ok};
    std::string error_code;
    std::string error_message;
    ComponentRangeSplitTarget rejected_target;
    std::string inventory_revision_id;
    std::vector<ComponentRangeSplitItem> items;
    ComponentRangeSplitTotals totals;
    std::vector<ComponentRangeSplitWorkItem> work_items;
};

[[nodiscard]] ComponentRangeSplitAnalysis analyze_component_range_splits(
    const Json::Value& current,
    const inventory::InventoryRevision& revision,
    std::vector<ComponentRangeSplitTarget> targets,
    std::size_t max_range_count = 500,
    std::size_t max_result_defects = 2000);

[[nodiscard]] ComponentRangeSplitPlan materialize_component_range_splits(
    const Json::Value& current,
    const ComponentRangeSplitAnalysis& analysis);

[[nodiscard]] ComponentRangeSplitPlan plan_component_range_splits(
    const Json::Value& current,
    const inventory::InventoryRevision& revision,
    std::vector<ComponentRangeSplitTarget> targets,
    std::size_t max_range_count = 500,
    std::size_t max_result_defects = 2000);

}  // namespace bridge_report::review
