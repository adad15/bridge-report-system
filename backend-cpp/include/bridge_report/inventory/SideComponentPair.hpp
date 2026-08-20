#pragma once

#include <optional>
#include <string>

#include "bridge_report/inventory/ComponentInventoryModels.hpp"

namespace bridge_report::inventory {

// 一个部件类别下"左右各一件"的构件对。左右由编号里的"左/右"定，不靠顺序。
struct SideComponentPair {
    std::string left_bridge_component_id;
    std::string right_bridge_component_id;
    std::string left_component_number;
    std::string right_component_number;
};

/**
 * @brief 找出某部件类别下可作为"两侧"整体绑定的左右构件对。
 *
 * 两个条件都满足才返回：
 *   1. 该类别在放行名单内（本期：人行道、栏杆）；
 *   2. 该类别下可用构件恰好两件，且编号仅"左↔右"不同。
 *
 * 判定只看台账结构，不解析病害编号里的"两侧""全幅"等文字。
 */
[[nodiscard]] std::optional<SideComponentPair> find_side_component_pair(
    const InventoryRevision& revision,
    const std::string& standard_component_category_id);

// 放行名单里是否含该类别。单独暴露供测试直接锁住名单本身。
[[nodiscard]] bool side_pair_category_allowed(
    const std::string& standard_component_category_id);

}  // namespace bridge_report::inventory
