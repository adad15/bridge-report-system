#pragma once

#include <optional>
#include <string>
#include <vector>

#include "bridge_report/rating_tree/RatingTreeModels.hpp"

namespace bridge_report::rating_tree {

/// 分层确定性匹配的统一结果类型。
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

/// 单条病害的候选池过滤与分层规则解析。
///
/// 固定顺序：适用范围过滤 -> 规范名称精确 -> 正式受控别名 -> 受控关键词 ->
/// 候选推荐。高优先级已经得到唯一合法结果时不再用低优先级规则替换。
/// 文字包含、编辑距离等普通模糊只能产生候选，永远不能自动绑定。
class RatingTreeResolver {
public:
    [[nodiscard]] RatingTreeMatchResult resolve(
        const EffectiveRatingTree& tree,
        const RatingTreeMatchInput& input) const;
};

}  // namespace bridge_report::rating_tree
