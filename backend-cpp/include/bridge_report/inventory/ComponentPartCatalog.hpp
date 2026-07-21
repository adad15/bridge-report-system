#pragma once

#include <string>
#include <vector>

#include "bridge_report/inventory/NumberingTemplate.hpp"

namespace bridge_report::inventory {

// 一个部件的数量输入键（用户在向导里逐个填的数量维），按模板里 Count 占位符出现顺序。
struct PartCountInput {
    std::string key;
    std::string label;
};

struct CatalogPart {
    std::string part_key;                        // 稳定键，如 "beam.girder"
    std::string default_name;                    // 默认现场名，可被用户改
    std::string standard_component_category_id;  // 规范评定类别
    std::string structure_part;                  // superstructure/substructure/deck_system
    std::string number_template;                 // 含 {span}/{c1}…/{name}
    std::vector<PartCountInput> count_inputs;     // Count 占位符对应的用户数量键
    bool provisional{false};                      // 临时编号形状（待真实报告校准）

    // 填入各 Count 数量（{name} 保持原样）。
    NumberingTemplate number_template_with_counts(const std::vector<int>& counts) const;
    // 先用现场名替换 {name}（空则用默认名），再填 Count 数量。
    NumberingTemplate number_template_with(const std::string& name,
                                           const std::vector<int>& counts) const;
};

// 全桥型部件全集（按 standard_component_category_id 扁平铺开）。
// "某桥型有哪些部件" 由端点/校验用规范包 taxonomy 对该桥型 generatable 求交集派生，
// 目录本身不写桥型清单。
const std::vector<CatalogPart>& component_parts();
const CatalogPart* find_part(const std::vector<CatalogPart>& parts, const std::string& key);

}  // namespace bridge_report::inventory
