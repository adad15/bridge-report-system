#include "bridge_report/rating_tree/RatingTreeResolver.hpp"

#include <algorithm>
#include <cctype>

namespace bridge_report::rating_tree {

namespace {

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

RatingTreeResolution from_candidates(
    const EffectiveRatingTree& tree,
    std::vector<std::string> candidates,
    std::string method,
    const std::string& evidence) {
    if (candidates.empty()) {
        return {};
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    if (candidates.size() != 1) {
        return {
            RatingTreeResolutionStatus::ambiguous,
            std::nullopt,
            std::move(method),
            std::move(candidates),
            std::nullopt,
            {},
            evidence};
    }
    const auto& node = tree.nodes.at(candidates.front());
    return {
        RatingTreeResolutionStatus::resolved,
        candidates.front(),
        std::move(method),
        std::move(candidates),
        node.h21_indicator_id,
        node.allowed_scales,
        evidence};
}

std::string normalized_name(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isspace(ch) || ch == ',' || ch == '.' || ch == '-' ||
            ch == '_' || ch == '/' || ch == '(' || ch == ')') {
            continue;
        }
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

bool fuzzy_related(const std::string& left, const std::string& right) {
    const auto normalized_left = normalized_name(left);
    const auto normalized_right = normalized_name(right);
    if (normalized_left.empty() || normalized_right.empty()) {
        return false;
    }
    return normalized_left.find(normalized_right) != std::string::npos ||
        normalized_right.find(normalized_left) != std::string::npos;
}

}  // namespace

RatingTreeResolution RatingTreeResolver::resolve(
    const EffectiveRatingTree& tree,
    const std::string& bridge_type_id,
    const std::string& component_category_id,
    const std::string& original_defect_name) const {
    std::vector<std::string> exact;
    for (const auto& [id, node] : tree.nodes) {
        if (applicable(node, bridge_type_id, component_category_id) &&
            node.display_name == original_defect_name) {
            exact.push_back(id);
        }
    }
    if (!exact.empty()) {
        return from_candidates(
            tree, std::move(exact), "exact", "病害名称与当前范围内的评定树节点完全一致。");
    }

    std::vector<std::string> aliases;
    for (const auto& alias : tree.aliases) {
        if (alias.alias == original_defect_name &&
            alias.bridge_type_id == bridge_type_id &&
            alias.component_category_id == component_category_id) {
            const auto node = tree.nodes.find(alias.target_node_id);
            if (node != tree.nodes.end() &&
                applicable(node->second, bridge_type_id, component_category_id)) {
                aliases.push_back(alias.target_node_id);
            }
        }
    }
    if (!aliases.empty()) {
        return from_candidates(
            tree,
            std::move(aliases),
            "controlled_alias",
            "病害名称命中当前桥型与构件范围内的受控别名。");
    }

    std::vector<std::string> fuzzy;
    for (const auto& [id, node] : tree.nodes) {
        if (applicable(node, bridge_type_id, component_category_id) &&
            fuzzy_related(node.display_name, original_defect_name)) {
            fuzzy.push_back(id);
        }
    }
    std::sort(fuzzy.begin(), fuzzy.end());
    fuzzy.erase(std::unique(fuzzy.begin(), fuzzy.end()), fuzzy.end());
    if (fuzzy.empty()) {
        return {};
    }
    return {
        RatingTreeResolutionStatus::candidates,
        std::nullopt,
        "fuzzy_candidate",
        std::move(fuzzy),
        std::nullopt,
        {},
        "文字相似仅用于列出候选，必须由用户确认后才能绑定。"};
}

}  // namespace bridge_report::rating_tree
