#include "bridge_report/rating_tree/RatingTreeCompiler.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

#include "bridge_report/auth/PasswordHash.hpp"

namespace bridge_report::rating_tree {

namespace {

struct H21Indicator {
    const Json::Value* indicator{nullptr};
    const Json::Value* applicable_components{nullptr};
};

std::optional<H21Indicator> find_indicator(
    const standards::StandardPackage& package,
    const std::string& indicator_id) {
    for (const auto& [_, definition] : package.definitions) {
        const auto& payload = definition.payload;
        if (!payload["indicators"].isArray() ||
            !payload["applicable_component_ids"].isArray()) {
            continue;
        }
        for (const auto& indicator : payload["indicators"]) {
            if (indicator["id"].isString() &&
                indicator["id"].asString() == indicator_id) {
                return H21Indicator{&indicator, &payload["applicable_component_ids"]};
            }
        }
    }
    return std::nullopt;
}

const Json::Value* find_definition(
    const standards::StandardPackage& package,
    const std::string& id) {
    const auto it = package.definitions.find(id);
    return it == package.definitions.end() ? nullptr : &it->second.payload;
}

bool contains(const Json::Value& array, const std::string& value) {
    return std::any_of(array.begin(), array.end(), [&](const Json::Value& item) {
        return item.isString() && item.asString() == value;
    });
}

std::string canonical_tree(const EffectiveRatingTree& tree) {
    Json::Value document;
    document["tree_code"] = tree.version.tree_code;
    document["tree_name"] = tree.version.tree_name;
    document["package_version"] = tree.version.package_version;
    document["h21_standard_id"] = tree.version.h21_standard_id;
    document["h21_package_version"] = tree.version.h21_package_version;
    document["h21_content_checksum"] = tree.version.h21_content_checksum;
    document["maintenance_standard_id"] =
        tree.version.maintenance_standard_id.has_value()
        ? Json::Value(*tree.version.maintenance_standard_id)
        : Json::Value();
    document["maintenance_package_version"] =
        tree.version.maintenance_package_version.has_value()
        ? Json::Value(*tree.version.maintenance_package_version)
        : Json::Value();
    document["maintenance_content_checksum"] =
        tree.version.maintenance_content_checksum.has_value()
        ? Json::Value(*tree.version.maintenance_content_checksum)
        : Json::Value();
    document["organization_content_checksum"] =
        tree.version.organization_content_checksum;
    document["nodes"] = Json::Value(Json::arrayValue);
    for (const auto& [_, node] : tree.nodes) {
        Json::Value value;
        value["id"] = node.id;
        value["parent_id"] =
            node.parent_id.has_value() ? Json::Value(*node.parent_id) : Json::Value();
        value["display_name"] = node.display_name;
        value["node_type"] = to_string(node.node_type);
        value["sort_order"] = node.sort_order;
        value["scoring_mode"] = to_string(node.scoring_mode);
        value["h21_indicator_id"] = node.h21_indicator_id.has_value()
            ? Json::Value(*node.h21_indicator_id)
            : Json::Value();
        value["is_selectable"] = node.is_selectable;
        value["is_scoring"] = node.is_scoring;
        value["organization_note"] = node.organization_note;
        value["h21_indicator_name"] = node.h21_indicator_name;
        value["h21_source_table"] = node.h21_source_table;
        value["bridge_type_ids"] = Json::Value(Json::arrayValue);
        for (const auto& id : node.bridge_type_ids) {
            value["bridge_type_ids"].append(id);
        }
        value["component_category_ids"] = Json::Value(Json::arrayValue);
        for (const auto& id : node.component_category_ids) {
            value["component_category_ids"].append(id);
        }
        value["source_ids"] = Json::Value(Json::arrayValue);
        for (const auto& id : node.source_ids) {
            value["source_ids"].append(id);
        }
        value["allowed_scales"] = Json::Value(Json::arrayValue);
        value["scale_descriptions"] = Json::Value(Json::objectValue);
        value["deduction_points"] = Json::Value(Json::objectValue);
        for (const auto scale : node.allowed_scales) {
            const auto key = std::to_string(scale);
            value["allowed_scales"].append(scale);
            if (const auto it = node.scale_descriptions.find(scale);
                it != node.scale_descriptions.end()) {
                value["scale_descriptions"][key] = it->second;
            }
            if (const auto it = node.deduction_points.find(scale);
                it != node.deduction_points.end()) {
                value["deduction_points"][key] = it->second;
            }
        }
        document["nodes"].append(value);
    }
    document["aliases"] = Json::Value(Json::arrayValue);
    auto aliases = tree.aliases;
    std::sort(
        aliases.begin(),
        aliases.end(),
        [](const RatingTreeAlias& left, const RatingTreeAlias& right) {
            return std::tie(
                       left.bridge_type_id,
                       left.component_category_id,
                       left.alias,
                       left.target_node_id) <
                std::tie(
                       right.bridge_type_id,
                       right.component_category_id,
                       right.alias,
                       right.target_node_id);
        });
    for (const auto& alias : aliases) {
        Json::Value value;
        value["alias"] = alias.alias;
        value["target_node_id"] = alias.target_node_id;
        value["bridge_type_id"] = alias.bridge_type_id;
        value["component_category_id"] = alias.component_category_id;
        document["aliases"].append(value);
    }
    // 没有关键词规则时整个键都不写：多一个空数组会改变所有既有评定树的内容摘要，
    // 而"没有规则"和"规则为空"本来就是同一份内容。
    auto keyword_rules = tree.keyword_rules;
    if (!keyword_rules.empty()) document["keyword_rules"] = Json::Value(Json::arrayValue);
    std::sort(
        keyword_rules.begin(),
        keyword_rules.end(),
        [](const RatingTreeKeywordRule& left, const RatingTreeKeywordRule& right) {
            return std::tie(left.sort_order, left.rule_id) <
                std::tie(right.sort_order, right.rule_id);
        });
    for (const auto& rule : keyword_rules) {
        Json::Value value;
        value["rule_id"] = rule.rule_id;
        value["target_node_id"] = rule.target_node_id;
        value["bridge_type_id"] = rule.bridge_type_id;
        value["component_category_id"] = rule.component_category_id;
        value["auto_bind"] = rule.auto_bind;
        value["sort_order"] = rule.sort_order;
        value["rule_note"] = rule.rule_note;
        value["positive_keywords"] = Json::Value(Json::arrayValue);
        for (const auto& keyword : rule.positive_keywords) {
            value["positive_keywords"].append(keyword);
        }
        value["excluded_keywords"] = Json::Value(Json::arrayValue);
        for (const auto& keyword : rule.excluded_keywords) {
            value["excluded_keywords"].append(keyword);
        }
        document["keyword_rules"].append(value);
    }
    document["sources"] = Json::Value(Json::arrayValue);
    for (const auto& [_, source] : tree.sources) {
        Json::Value value;
        value["id"] = source.id;
        value["source_type"] = source.source_type;
        value["title"] = source.title;
        value["reference"] = source.reference;
        document["sources"].append(value);
    }
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, document);
}

}  // namespace

RatingTreeCompileResult RatingTreeCompiler::compile(
    const standards::StandardPackage& h21_package,
    const standards::StandardPackage* maintenance_package,
    const RatingTreeExtensionPackage& extension) const {
    RatingTreeCompileResult result;
    if (h21_package.manifest.family !=
        standards::StandardFamily::technical_condition) {
        result.issues.push_back(
            {"rating_tree_h21_package_invalid", "评定树必须使用技术状况规范包作为评分来源。"});
        return result;
    }
    if (maintenance_package != nullptr &&
        maintenance_package->manifest.family !=
            standards::StandardFamily::maintenance) {
        result.issues.push_back({
            "rating_tree_maintenance_package_invalid",
            "评定树的检查养护来源必须使用 maintenance 规范包。"});
        return result;
    }

    EffectiveRatingTree tree;
    tree.version.tree_code = extension.manifest.tree_code;
    tree.version.tree_name = extension.manifest.tree_name;
    tree.version.package_version = extension.manifest.package_version;
    tree.version.h21_standard_id = h21_package.manifest.standard_id;
    tree.version.h21_package_version = h21_package.manifest.package_version;
    tree.version.h21_content_checksum = h21_package.manifest.content_checksum;
    tree.version.organization_content_checksum =
        extension.manifest.content_checksum;
    if (maintenance_package != nullptr) {
        tree.version.maintenance_standard_id =
            maintenance_package->manifest.standard_id;
        tree.version.maintenance_package_version =
            maintenance_package->manifest.package_version;
        tree.version.maintenance_content_checksum =
            maintenance_package->manifest.content_checksum;
    }
    tree.aliases = extension.aliases;
    tree.keyword_rules = extension.keyword_rules;
    tree.sources = extension.sources;

    for (const auto& [id, source] : extension.nodes) {
        EffectiveRatingTreeNode node;
        node.id = source.id;
        node.parent_id = source.parent_id;
        node.display_name = source.display_name;
        node.node_type = source.node_type;
        node.sort_order = source.sort_order;
        node.bridge_type_ids = source.bridge_type_ids;
        node.component_category_ids = source.component_category_ids;
        node.scoring_mode = source.scoring_mode;
        node.h21_indicator_id = source.h21_indicator_id;
        node.is_selectable = source.is_selectable;
        node.organization_note = source.organization_note;
        node.source_ids = source.source_ids;
        node.is_scoring =
            source.scoring_mode != RatingTreeScoringMode::non_scoring;

        if (node.is_scoring) {
            if (!node.h21_indicator_id.has_value()) {
                result.issues.push_back({
                    "rating_tree_h21_indicator_missing",
                    "参与评分的评定树节点必须引用 H21 指标。"});
                return result;
            }
            const auto resolved = find_indicator(
                h21_package, *node.h21_indicator_id);
            if (!resolved.has_value()) {
                result.issues.push_back({
                    "rating_tree_h21_indicator_missing",
                    "评定树节点引用的 H21 指标不存在。"});
                return result;
            }
            for (const auto& component_id : node.component_category_ids) {
                if (!contains(*resolved->applicable_components, component_id)) {
                    result.issues.push_back({
                        "rating_tree_h21_indicator_not_applicable",
                        "H21 指标不适用于评定树节点 " + node.id +
                            " 声明的构件 " + component_id + "。"});
                    return result;
                }
            }
            const auto& indicator = *resolved->indicator;
            if (!indicator["name"].isString() ||
                !indicator["allowed_scales"].isArray() ||
                !indicator["scale_descriptions"].isObject() ||
                !indicator["deduction_rule_id"].isString()) {
                result.issues.push_back({
                    "rating_tree_h21_rule_incomplete",
                    "H21 指标缺少名称、标度说明或扣分规则。"});
                return result;
            }
            node.h21_indicator_name = indicator["name"].asString();
            if (indicator["source_table"].isString()) {
                node.h21_source_table = indicator["source_table"].asString();
            }
            const auto* deduction = find_definition(
                h21_package, indicator["deduction_rule_id"].asString());
            if (deduction == nullptr || !(*deduction)["points"].isObject()) {
                result.issues.push_back({
                    "rating_tree_h21_rule_incomplete",
                    "H21 指标引用的扣分规则不存在或不完整。"});
                return result;
            }
            for (const auto& scale_value : indicator["allowed_scales"]) {
                if (!scale_value.isInt()) {
                    result.issues.push_back({
                        "rating_tree_h21_rule_incomplete", "H21 标度必须为整数。"});
                    return result;
                }
                const int scale = scale_value.asInt();
                const auto key = std::to_string(scale);
                if (!indicator["scale_descriptions"][key].isString() ||
                    indicator["scale_descriptions"][key].asString().empty() ||
                    !(*deduction)["points"][key].isInt()) {
                    result.issues.push_back({
                        "rating_tree_h21_rule_incomplete",
                        "H21 每个允许标度都必须有判定文字和扣分。"});
                    return result;
                }
                node.allowed_scales.push_back(scale);
                node.scale_descriptions.emplace(
                    scale, indicator["scale_descriptions"][key].asString());
                node.deduction_points.emplace(
                    scale, (*deduction)["points"][key].asInt());
            }
        } else {
            node.h21_indicator_id.reset();
            node.is_scoring = false;
        }
        tree.nodes.emplace(id, std::move(node));
    }

    for (const auto& alias : tree.aliases) {
        const auto target = tree.nodes.find(alias.target_node_id);
        if (target == tree.nodes.end() || !target->second.is_selectable) {
            result.issues.push_back({
                "rating_tree_alias_target_invalid",
                "受控别名必须指向可选择的评定树病害节点。"});
            return result;
        }
    }

    for (const auto& rule : tree.keyword_rules) {
        const auto target = tree.nodes.find(rule.target_node_id);
        if (target == tree.nodes.end() || !target->second.is_selectable) {
            result.issues.push_back({
                "rating_tree_keyword_rule_target_invalid",
                "受控关键词规则必须指向可选择的评定树病害节点。"});
            return result;
        }
    }

    tree.version.tree_content_checksum =
        "sha256:" + bridge_report::auth::sha256_hex(canonical_tree(tree));
    result.tree = std::move(tree);
    return result;
}

}  // namespace bridge_report::rating_tree
