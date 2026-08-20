#include "bridge_report/inventory/SideComponentPair.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "bridge_report/inventory/ComponentMatcher.hpp"

namespace bridge_report::inventory {
namespace {

// 放行名单。本期只给人行道与栏杆。
//
// 为什么用名单，而不是只看"类别下恰好一对"这条结构规则：结构规则会随台账勾选
// 情况浮动——某桥若只勾了 0# 台的左右翼墙，翼墙类别恰好剩两件成对，选项就会
// 自己冒出来。放行范围应当是一处显式、可预期的决定，不该由某座桥勾了什么来定。
// 日后放开新部件 = 改这份名单，一处可见的改动。
//
// 键取标准部件类别 id，不用报告里的部件名称字符串：后者是展示文本，不同报告
// 的写法可能不一致（"栏杆、护栏" / "护栏"）。
//
// 名单放代码里不放规则包：这是界面放行范围，不是评分规则，不该占用不可变规则包
// 的版本号。
constexpr std::array<const char*, 2> kAllowedCategories{
    "h21.component.deck.sidewalk",
    "h21.component.deck.railing",
};

constexpr const char* kLeft = "左";
constexpr const char* kRight = "右";

std::string replace_all(std::string value, const std::string& from, const std::string& to) {
    std::size_t offset = 0;
    while ((offset = value.find(from, offset)) != std::string::npos) {
        value.replace(offset, from.size(), to);
        offset += to.size();
    }
    return value;
}

bool contains(const std::string& value, const char* needle) {
    return value.find(needle) != std::string::npos;
}

}  // namespace

bool side_pair_category_allowed(const std::string& standard_component_category_id) {
    return std::any_of(
        kAllowedCategories.begin(), kAllowedCategories.end(),
        [&](const char* allowed) { return standard_component_category_id == allowed; });
}

std::optional<SideComponentPair> find_side_component_pair(
    const InventoryRevision& revision,
    const std::string& standard_component_category_id
) {
    if (!side_pair_category_allowed(standard_component_category_id)) return std::nullopt;

    // 件数口径与匹配一致：启用且有生效映射的才算数。
    std::vector<const InventoryEntry*> members;
    for (const auto* entry : usable_inventory_entries(revision)) {
        if (active_inventory_mapping(*entry)->standard_component_category_id
            == standard_component_category_id) {
            members.push_back(entry);
        }
    }
    if (members.size() != 2) return std::nullopt;

    // 左右由编号里的"左/右"定，不靠台账顺序。带了两个方位字的编号无法归边，弃权。
    const InventoryEntry* left = nullptr;
    const InventoryEntry* right = nullptr;
    for (const auto* entry : members) {
        const bool has_left = contains(entry->component_number, kLeft);
        const bool has_right = contains(entry->component_number, kRight);
        if (has_left && !has_right) {
            left = entry;
        } else if (has_right && !has_left) {
            right = entry;
        }
    }
    if (left == nullptr || right == nullptr) return std::nullopt;

    // 必须是同一个记号的左右两面：把左件编号里的"左"换成"右"后应与右件相等。
    // 少了这一步，"左侧栏杆 + 右侧扶手"这种凑数的两件也会被当成一对。
    if (normalize_component_number(replace_all(left->component_number, kLeft, kRight))
        != normalize_component_number(right->component_number)) {
        return std::nullopt;
    }

    SideComponentPair pair;
    pair.left_bridge_component_id = left->bridge_component_id;
    pair.right_bridge_component_id = right->bridge_component_id;
    pair.left_component_number = left->component_number;
    pair.right_component_number = right->component_number;
    return pair;
}

}  // namespace bridge_report::inventory
