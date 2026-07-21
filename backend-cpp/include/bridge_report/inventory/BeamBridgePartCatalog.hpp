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

struct BeamBridgePart {
    std::string part_key;                        // 稳定键，如 "beam.girder"
    std::string default_name;                    // 默认现场名，可被用户改
    std::string standard_component_category_id;  // 规范评定类别
    std::string structure_part;                  // superstructure/substructure/deck_system
    std::string number_template;                 // 含 {span}/{c1}…/{name}
    std::vector<PartCountInput> count_inputs;     // Count 占位符对应的用户数量键

    // 填入各 Count 数量（{name} 保持原样）。
    NumberingTemplate number_template_with_counts(const std::vector<int>& counts) const;
    // 先用现场名替换 {name}（空则用默认名），再填 Count 数量。
    NumberingTemplate number_template_with(const std::string& name,
                                           const std::vector<int>& counts) const;
};

const std::vector<BeamBridgePart>& beam_bridge_parts();
const BeamBridgePart* find_part(const std::vector<BeamBridgePart>& parts, const std::string& key);

}  // namespace bridge_report::inventory
