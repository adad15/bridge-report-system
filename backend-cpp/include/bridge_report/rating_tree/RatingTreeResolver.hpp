#pragma once

#include <optional>
#include <string>
#include <vector>

#include "bridge_report/rating_tree/RatingTreeModels.hpp"

namespace bridge_report::rating_tree {

/// 来源身份精确匹配的统一结果类型。
enum class RatingTreeMatchOutcome {
    auto_bound,            // 唯一且规则允许自动绑定
    candidates,            // 只能给候选，由人工选择
    composite,             // 同一条记录明确命中两个及以上不同病害节点
    unmatched,             // 适用范围内没有任何规则命中
    prerequisite_missing,  // 构件、评定树等前置依赖缺失
    service_error,         // 目录不可用或匹配器自身失败
};

/// 匹配失败与需人工处理的原因码。页面据此区分依赖缺失、服务故障和真的没规则。
inline constexpr const char* kReasonComponentNotBound = "component_not_bound";
inline constexpr const char* kReasonRatingTreeNotBound = "rating_tree_not_bound";
inline constexpr const char* kReasonComponentCategoryUnmapped =
    "component_category_unmapped";
inline constexpr const char* kReasonCatalogUnavailable =
    "rating_tree_catalog_unavailable";
inline constexpr const char* kReasonNoMatchingRule = "no_matching_rule";
inline constexpr const char* kReasonMultipleCandidates = "multiple_candidates";
inline constexpr const char* kReasonCompositeDefect = "composite_defect";
inline constexpr const char* kReasonMatcherFailed = "matcher_failed";
/// 结果唯一但规则只允许推荐，仍须人工确认。
inline constexpr const char* kReasonCandidateRequiresReview =
    "candidate_requires_review";
/// 区间或多目标展开后，各实例落在不同类别、匹到不同结果。校对页按来源病害显示一行
/// （§22.6），一行给不出互相矛盾的自动结果，只能请人逐个实例处理。
inline constexpr const char* kReasonInstancesDisagree = "instances_disagree";

/// 候选只在当前页面展示，绝不写入正式 rating_tree_node_id。
struct RatingTreeMatchCandidate {
    std::string node_id;
    std::string display_name;
    std::string match_method;
    std::string evidence;
};

struct RatingTreeMatchInput {
    std::string bridge_type_id;
    std::string component_category_id;
    std::string defect_type;
    std::string defect_description;
    std::string defect_location;
    // 来源软件的原始分组与指标身份；不与 H21 指标 ID 混用。
    std::string source_defect_group_id;
    std::string source_defect_group_number;
    std::string source_defect_indicator_id;
    std::string source_defect_indicator_number;
};

struct RatingTreeMatchResult {
    RatingTreeMatchOutcome outcome{RatingTreeMatchOutcome::unmatched};
    std::optional<std::string> node_id;  // 仅 auto_bound 有值
    std::string match_method;
    std::string match_evidence;
    std::vector<RatingTreeMatchCandidate> candidates;  // 最多三个，临时结果
    std::string reason_code;
    std::string reason_message;
    std::optional<std::string> h21_indicator_id;
    std::vector<int> allowed_scales;
};

std::string to_string(RatingTreeMatchOutcome value);

/**
 * @brief 该节点是否适用于给定桥型与规范构件类别。
 *
 * 自动匹配用它收窄候选范围，人工选择与手工新增用它校验用户选中的节点。三处必须共用
 * 一份：各写一份的话，会出现"自动匹配挑不到、人工却能选进去"的节点，而它到了评定阶段
 * 才会以"该构件不适用此病害"暴露出来。
 */
[[nodiscard]] bool node_applies_to(
    const EffectiveRatingTreeNode& node,
    const std::string& bridge_type_id,
    const std::string& component_category_id);

/// 单条病害按当前桥型/构件范围过滤后，仅以来源分组+指标精确解析。
/// 先匹配原始 ID 对，再匹配编号对；病害名称、描述、别名和关键词均不参与。
class RatingTreeResolver {
public:
    [[nodiscard]] RatingTreeMatchResult resolve(
        const EffectiveRatingTree& tree,
        const RatingTreeMatchInput& input) const;
};

}  // namespace bridge_report::rating_tree
