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

/**
 * @brief 把一行病害拆到人工选定的多个实际构件上（界面上的"两侧"绑定）。
 *
 * 产出与 analyze_component_range_splits 同型的 analysis，区别只在 matches 的来源：
 * 那边来自编号范围展开，这边来自人工在下拉里选定的构件。因此可以原样交给
 * materialize_component_range_splits——拆分溯源、照片复制、待确认状态、警告改写
 * 全部复用，拆分逻辑一行不必新写。
 *
 * 选定的构件须属于该台账版本、启用、有生效映射，且类别与 target.part_name 的
 * 对照相符；至少两个且不得重复。任一不满足则整批拒绝。
 */
/**
 * @brief 把拆分溯源里的哨兵值换成真实的操作 id、操作人与时间。
 *
 * 拆分本身是纯函数，拿不到这三样，于是先写哨兵，由写事务在落库前补齐。
 * 哨兵常量与替换逻辑必须同处一地——分开放的话改了一处、另一处会静默不再匹配，
 * 溯源里就留下 __range_split_user__ 这种垃圾，而且没有任何报错。
 */
void stamp_split_origin(
    Json::Value& result,
    const std::string& operation_id,
    const std::string& user_id,
    const std::string& operated_at);

[[nodiscard]] ComponentRangeSplitAnalysis analyze_component_multi_bind(
    const Json::Value& current,
    const inventory::InventoryRevision& revision,
    const ComponentRangeSplitTarget& target,
    const std::vector<std::string>& bridge_component_ids);

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
