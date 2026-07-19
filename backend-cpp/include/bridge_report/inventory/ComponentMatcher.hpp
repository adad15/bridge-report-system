#pragma once

#include <optional>
#include <string>
#include <vector>

#include "bridge_report/inventory/ComponentInventoryModels.hpp"

namespace bridge_report::inventory {

struct ConfirmedComponentAlias {
    std::string bridge_component_id;
    std::string alias_text;
};

struct DefectComponentText {
    std::string component_number;
    std::string component_name;
};

enum class ComponentMatchMethod {
    None,
    Exact,
    ConfirmedAlias,
    NormalizedCandidate,
};

struct ComponentMatchResult {
    ComponentMatchMethod method{ComponentMatchMethod::None};
    std::optional<InventoryEntry> matched_entry;
    std::optional<InventoryMapping> matched_mapping;
    std::vector<std::string> candidate_component_ids;
};

[[nodiscard]] std::string component_match_method_name(ComponentMatchMethod method);
[[nodiscard]] std::string normalize_component_number(const std::string& value);

/**
 * @brief 按“完全匹配 -> 已确认别名 -> 规范化编号候选”的固定顺序匹配病害。
 *
 * 只有已确认台账中的唯一完全匹配或唯一已确认别名匹配会自动落实际构件；
 * 规范化编号以及未确认台账只返回候选，必须人工选择。
 */
[[nodiscard]] ComponentMatchResult match_defect_component(
    const DefectComponentText& defect,
    const InventoryRevision& revision,
    const std::vector<ConfirmedComponentAlias>& confirmed_aliases
);

}  // namespace bridge_report::inventory
