#pragma once

#include <string>
#include <vector>

#include "bridge_report/standards/AssessmentModels.hpp"
#include "bridge_report/standards/StandardModels.hpp"

namespace bridge_report::standards {

/// 某个桥型下、一个部件类别的规范权重。
struct ComponentWeightEntry {
    std::string component_type_id;
    std::string component_type_name;
    StructurePart structure_part{StructurePart::superstructure};
    double configured_weight{0.0};
};

/**
 * @brief 桥型的完整部件权重表，按规范原表顺序。
 *
 * 桥上实际没有的部件（如无调治构造物）在这里同样出现——报告的「部件权重计算表」
 * 要把它们列出来并注明"无此构件"，权重重分配才说得通。评定结果里只有实际存在的
 * 部件，所以那张表凑不出来。
 *
 * 顺序即规范表的行序，也就是报告表的行序，因此保留权重集数组的原始次序；
 * 评定器内部按 id 查权重用的是 map，会把顺序打散，不能拿来出表。
 *
 * 桥型不存在或权重集缺失时返回空。
 */
std::vector<ComponentWeightEntry> component_weight_table(
    const StandardPackage& package, const std::string& bridge_type_id);

/**
 * @brief 部件类别的规范名称，如 h21.component.lower.pier -> 「桥墩」。
 *
 * 病害表的「部件名称」列要的就是这个 H21 部件泛称：盖梁归桥墩、铰缝归上部一般
 * 构件。它与 表4.1-2 的「评价部件」同源，读者才能顺着病害行找到对应的部件评分。
 *
 * 类别不在包里时返回空。
 */
std::string component_type_name(
    const StandardPackage& package, const std::string& component_type_id);

}  // namespace bridge_report::standards
