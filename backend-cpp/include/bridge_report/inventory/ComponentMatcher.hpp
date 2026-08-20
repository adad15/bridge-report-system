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
    Exact,           // 部件类别 + 归一化编号（保留类型词）精确
    ConfirmedAlias,  // 人工确认别名 + 归一化编号 兜底
};

struct ComponentMatchResult {
    ComponentMatchMethod method{ComponentMatchMethod::None};
    std::optional<InventoryEntry> matched_entry;
    std::optional<InventoryMapping> matched_mapping;
    std::vector<std::string> candidate_component_ids;
};

[[nodiscard]] std::string component_match_method_name(ComponentMatchMethod method);

/**
 * @brief 台账条目上的生效映射；没有则返回 nullptr。
 */
[[nodiscard]] const InventoryMapping* active_inventory_mapping(const InventoryEntry& entry);

/**
 * @brief 台账里"可用"的构件：启用 + 有生效映射。
 *
 * 匹配与侧别配对共用这一条。两处各写一份的话，对"这个构件算不算数"的答案迟早分叉。
 */
[[nodiscard]] std::vector<const InventoryEntry*> usable_inventory_entries(
    const InventoryRevision& revision);
[[nodiscard]] std::string normalize_component_number(const std::string& value);

/**
 * @brief 按“部件类别 + 编号 -> 已确认别名”匹配病害。
 *
 * 部件类别由报告“部件名称”经对照表解析（可多候选，靠台账实际类别消歧）；
 * 构件编号做无害归一化后精确比对（保留类型词），构件名不参与匹配。
 * 只有已确认台账中的唯一命中会自动落实际构件；否则返回候选，必须人工选择。
 */
[[nodiscard]] ComponentMatchResult match_defect_component(
    const DefectComponentText& defect,
    const InventoryRevision& revision,
    const std::vector<ConfirmedComponentAlias>& confirmed_aliases
);

}  // namespace bridge_report::inventory
