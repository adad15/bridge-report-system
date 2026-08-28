#pragma once

#include <string>

#include <json/value.h>

// 评分树解析的两个规范哈希（设计 §8.5）。
//
// 拆成两个而不是一个，是为了不必在"保住人工选择"和"检测构件适用性变化"之间二选一：
//
//   applicability_hash  评定树版本 + 目标构件 + 规范包 + 规范桥型 + 构件类别
//   match_input_hash    上述适用性输入 + 来源分组/指标身份 + 有效文字输入
//
// 自动结果：任一哈希不一致即失效并重新匹配。
// 人工结果：只有 applicability_hash 不一致才失效；文字变化保留节点并标记待复核。
namespace bridge_report::resolution {

/// 计算两个哈希所需的全部输入。文字三项必须传**有效值**（来源值叠加实例覆盖）。
struct RatingMatchHashInput {
    std::string source_candidate_id;
    std::string bridge_component_id;
    std::string technical_standard_package_id;
    std::string standard_bridge_type_id;
    std::string standard_component_category_id;
    std::string rating_tree_version_id;
    std::string source_defect_group_id;
    std::string source_defect_group_number;
    std::string source_defect_indicator_id;
    std::string source_defect_indicator_number;
    std::string defect_type;         // 有效值
    std::string defect_location;     // 有效值
    std::string defect_description;  // 有效值
};

struct RatingMatchHashes {
    std::string applicability_hash;
    std::string match_input_hash;
};

/**
 * @brief 从有效病害事实和解析上下文装配哈希输入。
 *
 * `effective_defect` 必须已经过 merge_effective_defect_facts；直接传来源病害会让
 * 同一条来源病害展开出的多个实例算出恒等的哈希，覆盖再怎么改也触发不了失效。
 */
[[nodiscard]] RatingMatchHashInput build_rating_match_hash_input(
    const Json::Value& effective_defect,
    const std::string& source_candidate_id,
    const std::string& bridge_component_id,
    const std::string& technical_standard_package_id,
    const std::string& standard_bridge_type_id,
    const std::string& standard_component_category_id,
    const std::string& rating_tree_version_id);

[[nodiscard]] RatingMatchHashes compute_rating_match_hashes(
    const RatingMatchHashInput& input);

}  // namespace bridge_report::resolution
