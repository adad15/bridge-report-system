#include "bridge_report/rating_tree/RatingTreeResolver.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <utility>

#include "bridge_report/rating_tree/RatingTreeMatchText.hpp"

namespace bridge_report::rating_tree {

namespace {

constexpr std::size_t kMaxCandidates = 3;

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool applicable(
    const EffectiveRatingTreeNode& node,
    const std::string& bridge_type_id,
    const std::string& component_category_id) {
    return node.node_type == RatingTreeNodeType::defect && node.is_selectable &&
        contains(node.bridge_type_ids, bridge_type_id) &&
        contains(node.component_category_ids, component_category_id);
}

std::string scope_evidence(const RatingTreeMatchInput& input) {
    return "构件适用依据：桥型 " + input.bridge_type_id + " 与规范构件类别 " +
        input.component_category_id + " 均在该节点的适用范围内。";
}

// 命中记录：同一节点可能被多条规则命中，保留优先级最高的一条作为证据。
struct NodeHit {
    std::string node_id;
    int layer{0};  // 1 精确 / 2 别名 / 3 关键词
    std::string match_method;
    std::string evidence;
    bool auto_bind{false};
};

void record_hit(std::map<std::string, NodeHit>& hits, NodeHit hit) {
    const auto existing = hits.find(hit.node_id);
    if (existing == hits.end()) {
        hits.emplace(hit.node_id, std::move(hit));
        return;
    }
    // 同一节点被多条规则命中时，只要有一条允许自动绑定就保留该资格。
    existing->second.auto_bind = existing->second.auto_bind || hit.auto_bind;
    if (hit.layer < existing->second.layer) {
        const bool auto_bind = existing->second.auto_bind;
        existing->second = std::move(hit);
        existing->second.auto_bind = auto_bind;
    }
}

bool keywords_present(
    const std::string& haystack,
    const std::vector<std::string>& keywords) {
    return std::all_of(
        keywords.begin(), keywords.end(), [&](const std::string& keyword) {
            const auto normalized = normalize_match_key(keyword);
            return !normalized.empty() &&
                haystack.find(normalized) != std::string::npos;
        });
}

bool any_keyword_present(
    const std::string& haystack,
    const std::vector<std::string>& keywords) {
    return std::any_of(
        keywords.begin(), keywords.end(), [&](const std::string& keyword) {
            const auto normalized = normalize_match_key(keyword);
            return !normalized.empty() &&
                haystack.find(normalized) != std::string::npos;
        });
}

std::string join_keywords(const std::vector<std::string>& keywords) {
    std::string joined;
    for (std::size_t index = 0; index < keywords.size(); ++index) {
        if (index != 0) joined += "、";
        joined += keywords[index];
    }
    return joined;
}

// 候选排序必须是确定性的：先按节点声明顺序，再按稳定 ID 兜底。
std::vector<RatingTreeMatchCandidate> to_candidates(
    const EffectiveRatingTree& tree,
    std::vector<NodeHit> hits) {
    std::sort(hits.begin(), hits.end(), [&](const NodeHit& left, const NodeHit& right) {
        const auto& left_node = tree.nodes.at(left.node_id);
        const auto& right_node = tree.nodes.at(right.node_id);
        return std::tie(left.layer, left_node.sort_order, left.node_id) <
            std::tie(right.layer, right_node.sort_order, right.node_id);
    });
    std::vector<RatingTreeMatchCandidate> candidates;
    for (const auto& hit : hits) {
        if (candidates.size() >= kMaxCandidates) break;
        candidates.push_back(
            {hit.node_id,
             tree.nodes.at(hit.node_id).display_name,
             hit.match_method,
             hit.evidence});
    }
    return candidates;
}

RatingTreeMatchResult bind_node(
    const EffectiveRatingTree& tree,
    const NodeHit& hit,
    const RatingTreeMatchInput& input) {
    const auto& node = tree.nodes.at(hit.node_id);
    RatingTreeMatchResult result;
    result.outcome = RatingTreeMatchOutcome::auto_bound;
    result.node_id = hit.node_id;
    result.match_method = hit.match_method;
    result.match_evidence = hit.evidence + " " + scope_evidence(input);
    result.h21_indicator_id = node.h21_indicator_id;
    result.allowed_scales = node.allowed_scales;
    result.candidates = {
        {hit.node_id, node.display_name, hit.match_method, result.match_evidence}};
    return result;
}

}  // namespace

std::string to_string(const RatingTreeMatchOutcome value) {
    switch (value) {
        case RatingTreeMatchOutcome::auto_bound: return "auto_bound";
        case RatingTreeMatchOutcome::candidates: return "candidates";
        case RatingTreeMatchOutcome::composite: return "composite";
        case RatingTreeMatchOutcome::prerequisite_missing: return "prerequisite_missing";
        case RatingTreeMatchOutcome::service_error: return "service_error";
        case RatingTreeMatchOutcome::unmatched: break;
    }
    return "unmatched";
}

RatingTreeMatchResult RatingTreeResolver::resolve(
    const EffectiveRatingTree& tree,
    const RatingTreeMatchInput& input) const {
    RatingTreeMatchResult result;
    result.reason_code = kReasonNoMatchingRule;
    result.reason_message = "当前构件适用范围内没有命中任何受控匹配规则。";

    // 1. 适用范围过滤：候选池成形前不得用病害文字搜全树。
    std::vector<const EffectiveRatingTreeNode*> pool;
    for (const auto& [id, node] : tree.nodes) {
        if (applicable(node, input.bridge_type_id, input.component_category_id)) {
            pool.push_back(&node);
        }
    }
    if (pool.empty()) {
        // 目录里根本没有可选节点是依赖问题，不能和"规则没覆盖"混成同一个未匹配。
        result.outcome = RatingTreeMatchOutcome::prerequisite_missing;
        result.reason_code = kReasonComponentCategoryUnmapped;
        result.reason_message =
            "当前评定树版本没有适用于该桥型与构件类别的可选择病害节点。";
        return result;
    }

    const auto type_key = normalize_match_key(input.defect_type);

    // 2. 规范名称精确匹配。
    if (!type_key.empty()) {
        std::vector<NodeHit> exact;
        for (const auto* node : pool) {
            if (normalize_match_key(node->display_name) == type_key) {
                exact.push_back(
                    {node->id,
                     1,
                     "exact",
                     "病害类型“" + input.defect_type + "”与规范病害名称完全一致。",
                     true});
            }
        }
        if (exact.size() == 1) return bind_node(tree, exact.front(), input);
        if (exact.size() > 1) {
            result.outcome = RatingTreeMatchOutcome::candidates;
            result.match_method = "fuzzy_candidate";
            result.candidates = to_candidates(tree, std::move(exact));
            result.reason_code = kReasonMultipleCandidates;
            result.reason_message = "同一名称对应多个可选择节点，请人工选择。";
            return result;
        }
    }

    // 3. 正式受控别名匹配（别名命中后仍重新校验目标节点是否适用）。
    if (!type_key.empty()) {
        std::map<std::string, NodeHit> alias_hits;
        for (const auto& alias : tree.aliases) {
            if (alias.bridge_type_id != input.bridge_type_id ||
                alias.component_category_id != input.component_category_id ||
                normalize_match_key(alias.alias) != type_key) {
                continue;
            }
            const auto node = tree.nodes.find(alias.target_node_id);
            if (node == tree.nodes.end() ||
                !applicable(
                    node->second, input.bridge_type_id, input.component_category_id)) {
                continue;
            }
            record_hit(
                alias_hits,
                {alias.target_node_id,
                 2,
                 "controlled_alias",
                 "病害类型“" + input.defect_type + "”命中受控别名“" + alias.alias +
                     "”。",
                 true});
        }
        if (alias_hits.size() == 1) {
            return bind_node(tree, alias_hits.begin()->second, input);
        }
        if (alias_hits.size() > 1) {
            std::vector<NodeHit> hits;
            for (auto& [_, hit] : alias_hits) hits.push_back(std::move(hit));
            result.outcome = RatingTreeMatchOutcome::candidates;
            result.match_method = "fuzzy_candidate";
            result.candidates = to_candidates(tree, std::move(hits));
            result.reason_code = kReasonMultipleCandidates;
            result.reason_message = "同一别名在当前范围内指向多个节点，请人工选择。";
            return result;
        }
    }

    // 4. 受控关键词匹配：病害类型为空或不规范时，用类型 + 描述 + 位置兜底。
    //    同时把片段级的精确/别名命中一并收集，才能识别组合病害。
    const auto search_key = normalize_match_key(
        input.defect_type + "," + input.defect_description + "," +
        input.defect_location);
    std::map<std::string, NodeHit> hits;
    for (const auto& segment : split_match_segments(
             input.defect_type + "," + input.defect_description + "," +
             input.defect_location)) {
        for (const auto* node : pool) {
            if (normalize_match_key(node->display_name) == segment) {
                record_hit(
                    hits,
                    {node->id,
                     1,
                     "exact",
                     "叙述片段“" + segment + "”与规范病害名称完全一致。",
                     true});
            }
        }
        for (const auto& alias : tree.aliases) {
            if (alias.bridge_type_id != input.bridge_type_id ||
                alias.component_category_id != input.component_category_id ||
                normalize_match_key(alias.alias) != segment) {
                continue;
            }
            const auto node = tree.nodes.find(alias.target_node_id);
            if (node == tree.nodes.end() ||
                !applicable(
                    node->second, input.bridge_type_id, input.component_category_id)) {
                continue;
            }
            record_hit(
                hits,
                {alias.target_node_id,
                 2,
                 "controlled_alias",
                 "叙述片段命中受控别名“" + alias.alias + "”。",
                 true});
        }
    }
    for (const auto& rule : tree.keyword_rules) {
        if (rule.bridge_type_id != input.bridge_type_id ||
            rule.component_category_id != input.component_category_id) {
            continue;
        }
        const auto node = tree.nodes.find(rule.target_node_id);
        if (node == tree.nodes.end() ||
            !applicable(
                node->second, input.bridge_type_id, input.component_category_id)) {
            continue;
        }
        if (!keywords_present(search_key, rule.positive_keywords)) continue;
        if (any_keyword_present(search_key, rule.excluded_keywords)) continue;
        record_hit(
            hits,
            {rule.target_node_id,
             3,
             "controlled_keyword",
             "命中受控关键词“" + join_keywords(rule.positive_keywords) +
                 "”（规则 " + rule.rule_id + "）。",
             rule.auto_bind});
    }

    if (hits.size() > 1) {
        // 组合病害：禁止自动选择其中一个节点，也不写入 rating_tree_node_id。
        std::vector<NodeHit> composite;
        for (auto& [_, hit] : hits) composite.push_back(std::move(hit));
        result.outcome = RatingTreeMatchOutcome::composite;
        result.match_method = "fuzzy_candidate";
        result.candidates = to_candidates(tree, std::move(composite));
        result.reason_code = kReasonCompositeDefect;
        result.reason_message =
            "同一条记录明确命中多个规范病害，需人工拆分或确认为单一病害。";
        return result;
    }
    if (hits.size() == 1) {
        const auto& hit = hits.begin()->second;
        if (hit.auto_bind) return bind_node(tree, hit, input);
        result.outcome = RatingTreeMatchOutcome::candidates;
        result.match_method = "fuzzy_candidate";
        result.candidates = {
            {hit.node_id,
             tree.nodes.at(hit.node_id).display_name,
             hit.match_method,
             hit.evidence + " " + scope_evidence(input)}};
        result.reason_code = kReasonCandidateRequiresReview;
        result.reason_message = "规则只允许推荐该病害，请人工确认后选择。";
        return result;
    }

    // 5. 候选推荐：普通文字包含只能给候选，永远不能作为自动绑定依据。
    std::vector<NodeHit> fuzzy;
    for (const auto* node : pool) {
        const auto name_key = normalize_match_key(node->display_name);
        if (name_key.empty() || type_key.empty()) continue;
        if (name_key.find(type_key) == std::string::npos &&
            type_key.find(name_key) == std::string::npos) {
            continue;
        }
        fuzzy.push_back(
            {node->id,
             4,
             "fuzzy_candidate",
             "病害文字与规范病害名称“" + node->display_name + "”部分相似。",
             false});
    }
    if (fuzzy.empty()) return result;
    result.outcome = RatingTreeMatchOutcome::candidates;
    result.match_method = "fuzzy_candidate";
    const auto candidate_count = fuzzy.size();
    result.candidates = to_candidates(tree, std::move(fuzzy));
    result.reason_code = candidate_count > 1 ? kReasonMultipleCandidates
                                             : kReasonCandidateRequiresReview;
    result.reason_message =
        "文字相似只作为候选，必须由用户确认后才能绑定评定树病害。";
    return result;
}

}  // namespace bridge_report::rating_tree
