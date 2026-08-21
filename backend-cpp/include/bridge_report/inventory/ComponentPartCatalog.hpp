#pragma once

#include <string>
#include <vector>

#include "bridge_report/inventory/NumberingTemplate.hpp"

namespace bridge_report::inventory {

// 一个部件的数量输入键（用户在向导里逐个填的数量维），按模板里 Count 占位符出现顺序。
struct PartCountInput {
    std::string key;
    std::string label;
    std::string hint;  // 口径说明，消除"数一跨还是两跨"这类歧义；空则不显示
};

struct CatalogPart {
    std::string part_key;                        // 稳定键，如 "beam.girder"
    std::string default_name;                    // 默认现场名，可被用户改
    std::string standard_component_category_id;  // 规范评定类别
    std::string structure_part;                  // superstructure/substructure/deck_system
    std::string number_template;                 // 含 {span}/{c1}…/{name}
    std::vector<PartCountInput> count_inputs;     // Count 占位符对应的用户数量键
    bool provisional{false};                      // 临时编号形状（待真实报告校准）
    // 展开出的位置在真实桥上不一定都存在（翼墙/锥坡/护坡），向导逐个给复选框，
    // 用户去掉的位置经 PartSelection::excluded_numbers 回传。
    bool instance_selectable{false};

    // 填入各 Count 数量（{name} 保持原样）。
    NumberingTemplate number_template_with_counts(const std::vector<int>& counts) const;
    // 先用现场名替换 {name}（空则用默认名），再填 Count 数量。
    NumberingTemplate number_template_with(const std::string& name,
                                           const std::vector<int>& counts) const;
};

// 全桥型部件全集（按 standard_component_category_id 扁平铺开）。
// "某桥型有哪些部件" 由端点/校验用规范包 taxonomy 对该桥型 generatable 求交集派生，
// 目录本身不写桥型清单。
/**
 * @brief 病害校对列表的部件走查顺序：上部结构 → 下部结构 → 桥面系，
 *        每一段内部按部件在本目录里出现的先后。
 *
 * 返回值只用来比大小，不对外承诺具体数值。未知类别排在最后。
 *
 * 为什么不直接用台账的 sort_order：台账是按向导勾选的次序生成的，实测某桥是
 * 下部→桥面系→上部，与校对时想要的走查顺序不一致。
 *
 * 为什么不按部件的中文名排：那是"现场名"，用户可以在向导里改——同一个部件在这座桥
 * 叫"板"、在另一座桥可能叫"梁"。类别 id 是规范派生的，稳定。
 *
 * 注意有三对部件共用同一个 H21 类别（墩柱/盖梁/系梁、台/台帽、锥坡/护坡），
 * 本函数分不开它们，需要由台账的 sort_order 做次级键——生成时正是按目录顺序展开的。
 */
[[nodiscard]] int component_review_rank(
    const std::string& structure_part,
    const std::string& standard_component_category_id);

const std::vector<CatalogPart>& component_parts();
const CatalogPart* find_part(const std::vector<CatalogPart>& parts, const std::string& key);

}  // namespace bridge_report::inventory
