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

[[nodiscard]] ComponentRangeSplitPlan plan_component_range_splits(
    const Json::Value& current,
    const inventory::InventoryRevision& revision,
    std::vector<ComponentRangeSplitTarget> targets,
    std::size_t max_range_count = 500,
    std::size_t max_result_defects = 2000);

}  // namespace bridge_report::review
