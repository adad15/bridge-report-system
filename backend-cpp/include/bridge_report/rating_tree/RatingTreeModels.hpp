#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

#include "bridge_report/standards/StandardModels.hpp"

namespace bridge_report::rating_tree {

enum class RatingTreeNodeType {
    root,
    bridge_type_group,
    structure_group,
    component_group,
    defect,
    placeholder,
};

enum class RatingTreeScoringMode {
    inherit_h21,
    reference_h21,
    non_scoring,
};

std::optional<RatingTreeNodeType> parse_rating_tree_node_type(const std::string& value);
std::string to_string(RatingTreeNodeType value);
std::optional<RatingTreeScoringMode> parse_rating_tree_scoring_mode(
    const std::string& value);
std::string to_string(RatingTreeScoringMode value);

struct RatingTreeExtensionManifest {
    std::string tree_code;
    std::string tree_name;
    std::string package_version;
    int contract_version{0};
    std::string status;
    std::string content_checksum;
    std::vector<std::string> entry_files;
};

struct RatingTreeExtensionNode {
    std::string id;
    std::optional<std::string> parent_id;
    std::string display_name;
    RatingTreeNodeType node_type{RatingTreeNodeType::placeholder};
    int sort_order{0};
    std::vector<std::string> bridge_type_ids;
    std::vector<std::string> component_category_ids;
    RatingTreeScoringMode scoring_mode{RatingTreeScoringMode::non_scoring};
    std::optional<std::string> h21_indicator_id;
    bool is_selectable{false};
    std::string organization_note;
    std::vector<std::string> source_ids;
};

struct RatingTreeAlias {
    std::string alias;
    std::string target_node_id;
    std::string bridge_type_id;
    std::string component_category_id;
};

// 受控关键词规则：与别名一样随不可变评定树版本发布，运行时不可编辑。
// 只有 auto_bind 为真、目标节点在当前适用范围内且最终结果唯一时才允许自动绑定；
// 其余情况只能作为候选提示。
struct RatingTreeKeywordRule {
    std::string rule_id;
    std::string target_node_id;
    std::string bridge_type_id;
    std::string component_category_id;
    std::vector<std::string> positive_keywords;
    std::vector<std::string> excluded_keywords;
    bool auto_bind{false};
    int sort_order{0};
    std::string rule_note;
};

struct RatingTreeSource {
    std::string id;
    std::string source_type;
    std::string title;
    std::string reference;
};

struct RatingTreeExtensionPackage {
    RatingTreeExtensionManifest manifest;
    std::map<std::string, RatingTreeExtensionNode> nodes;
    std::vector<RatingTreeAlias> aliases;
    std::vector<RatingTreeKeywordRule> keyword_rules;
    std::map<std::string, RatingTreeSource> sources;
    std::map<std::string, Json::Value> documents;
};

struct EffectiveRatingTreeVersion {
    std::string tree_code;
    std::string tree_name;
    std::string package_version;
    std::string h21_standard_id;
    std::string h21_package_version;
    std::string h21_content_checksum;
    std::optional<std::string> maintenance_standard_id;
    std::optional<std::string> maintenance_package_version;
    std::optional<std::string> maintenance_content_checksum;
    std::string organization_content_checksum;
    std::string tree_content_checksum;
};

struct EffectiveRatingTreeNode {
    std::string id;
    std::optional<std::string> parent_id;
    std::string display_name;
    RatingTreeNodeType node_type{RatingTreeNodeType::placeholder};
    int sort_order{0};
    std::vector<std::string> bridge_type_ids;
    std::vector<std::string> component_category_ids;
    RatingTreeScoringMode scoring_mode{RatingTreeScoringMode::non_scoring};
    std::optional<std::string> h21_indicator_id;
    bool is_selectable{false};
    bool is_scoring{false};
    std::string organization_note;
    std::vector<std::string> source_ids;
    std::vector<int> allowed_scales;
    std::map<int, std::string> scale_descriptions;
    std::map<int, int> deduction_points;
    std::string h21_indicator_name;
    std::string h21_source_table;
};

struct EffectiveRatingTree {
    EffectiveRatingTreeVersion version;
    std::map<std::string, EffectiveRatingTreeNode> nodes;
    std::vector<RatingTreeAlias> aliases;
    std::vector<RatingTreeKeywordRule> keyword_rules;
    std::map<std::string, RatingTreeSource> sources;
};

struct RatingTreeIssue {
    std::string code;
    std::string message;
};

}  // namespace bridge_report::rating_tree
