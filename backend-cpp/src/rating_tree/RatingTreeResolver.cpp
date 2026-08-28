#include "bridge_report/rating_tree/RatingTreeResolver.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

namespace bridge_report::rating_tree {

namespace {

constexpr std::size_t kMaxCandidates = 3;

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}


struct NodeHit {
    std::string node_id;
    std::string evidence;
};

std::vector<RatingTreeMatchCandidate> to_candidates(
    const EffectiveRatingTree& tree,
    std::vector<NodeHit> hits) {
    std::sort(hits.begin(), hits.end(), [&](const NodeHit& left, const NodeHit& right) {
        const auto& left_node = tree.nodes.at(left.node_id);
        const auto& right_node = tree.nodes.at(right.node_id);
        return std::tie(left_node.sort_order, left.node_id) <
            std::tie(right_node.sort_order, right.node_id);
    });
    std::vector<RatingTreeMatchCandidate> candidates;
    for (const auto& hit : hits) {
        if (candidates.size() >= kMaxCandidates) break;
        candidates.push_back({
            hit.node_id,
            tree.nodes.at(hit.node_id).display_name,
            "source_indicator",
            hit.evidence,
        });
    }
    return candidates;
}

RatingTreeMatchResult bind_node(
    const EffectiveRatingTree& tree,
    const NodeHit& hit) {
    const auto& node = tree.nodes.at(hit.node_id);
    RatingTreeMatchResult result;
    result.outcome = RatingTreeMatchOutcome::auto_bound;
    result.node_id = hit.node_id;
    result.match_method = "source_indicator";
    result.match_evidence = hit.evidence;
    result.h21_indicator_id = node.h21_indicator_id;
    result.allowed_scales = node.allowed_scales;
    result.candidates = {{
        hit.node_id,
        node.display_name,
        result.match_method,
        result.match_evidence,
    }};
    return result;
}

template <typename Predicate>
std::vector<NodeHit> mapping_hits(
    const std::vector<const EffectiveRatingTreeNode*>& pool,
    Predicate predicate,
    const std::string& evidence) {
    std::vector<NodeHit> hits;
    for (const auto* node : pool) {
        if (std::any_of(
                node->source_mappings.begin(),
                node->source_mappings.end(),
                predicate)) {
            hits.push_back({node->id, evidence});
        }
    }
    return hits;
}

std::optional<RatingTreeMatchResult> resolve_hits(
    const EffectiveRatingTree& tree,
    std::vector<NodeHit> hits) {
    if (hits.empty()) return std::nullopt;
    if (hits.size() == 1) return bind_node(tree, hits.front());

    RatingTreeMatchResult result;
    result.outcome = RatingTreeMatchOutcome::candidates;
    result.match_method = "source_indicator";
    result.candidates = to_candidates(tree, std::move(hits));
    result.reason_code = kReasonMultipleCandidates;
    result.reason_message =
        "来源分组与指标在当前构件下对应多个评定树病害，请人工选择。";
    return result;
}

}  // namespace

bool node_applies_to(
    const EffectiveRatingTreeNode& node,
    const std::string& bridge_type_id,
    const std::string& component_category_id) {
    return node.node_type == RatingTreeNodeType::defect && node.is_selectable &&
        contains(node.bridge_type_ids, bridge_type_id) &&
        contains(node.component_category_ids, component_category_id);
}

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
    result.reason_message =
        "来源分组与指标在当前评定树版本中没有精确对应关系。";

    std::vector<const EffectiveRatingTreeNode*> pool;
    for (const auto& [_, node] : tree.nodes) {
        if (node_applies_to(node, input.bridge_type_id, input.component_category_id)) {
            pool.push_back(&node);
        }
    }
    if (pool.empty()) {
        result.outcome = RatingTreeMatchOutcome::prerequisite_missing;
        result.reason_code = kReasonComponentCategoryUnmapped;
        result.reason_message =
            "当前评定树版本没有适用于该桥型与构件类别的可选择病害节点。";
        return result;
    }

    if (!input.source_defect_group_id.empty() &&
        !input.source_defect_indicator_id.empty()) {
        auto hits = mapping_hits(
            pool,
            [&](const RatingTreeSourceMapping& mapping) {
                return mapping.source_group_id == input.source_defect_group_id &&
                    mapping.source_indicator_id ==
                        input.source_defect_indicator_id;
            },
            "来源软件分组与指标 ID 精确命中评定树映射。");
        if (auto resolved = resolve_hits(tree, std::move(hits));
            resolved.has_value()) {
            return *resolved;
        }
    }

    if (!input.source_defect_group_number.empty() &&
        !input.source_defect_indicator_number.empty()) {
        auto hits = mapping_hits(
            pool,
            [&](const RatingTreeSourceMapping& mapping) {
                return mapping.source_group_number ==
                        input.source_defect_group_number &&
                    mapping.source_indicator_number ==
                        input.source_defect_indicator_number;
            },
            "来源软件分组编号与指标编号精确命中评定树映射。");
        if (auto resolved = resolve_hits(tree, std::move(hits));
            resolved.has_value()) {
            return *resolved;
        }
    }

    return result;
}

}  // namespace bridge_report::rating_tree
