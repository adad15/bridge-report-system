#include "bridge_report/rating_tree/RatingTreeModels.hpp"

namespace bridge_report::rating_tree {

std::optional<RatingTreeNodeType> parse_rating_tree_node_type(const std::string& value) {
    if (value == "root") return RatingTreeNodeType::root;
    if (value == "bridge_type_group") return RatingTreeNodeType::bridge_type_group;
    if (value == "structure_group") return RatingTreeNodeType::structure_group;
    if (value == "component_group") return RatingTreeNodeType::component_group;
    if (value == "defect") return RatingTreeNodeType::defect;
    if (value == "placeholder") return RatingTreeNodeType::placeholder;
    return std::nullopt;
}

std::string to_string(const RatingTreeNodeType value) {
    switch (value) {
    case RatingTreeNodeType::root: return "root";
    case RatingTreeNodeType::bridge_type_group: return "bridge_type_group";
    case RatingTreeNodeType::structure_group: return "structure_group";
    case RatingTreeNodeType::component_group: return "component_group";
    case RatingTreeNodeType::defect: return "defect";
    case RatingTreeNodeType::placeholder: return "placeholder";
    }
    return "placeholder";
}

std::optional<RatingTreeScoringMode> parse_rating_tree_scoring_mode(
    const std::string& value) {
    if (value == "inherit_h21") return RatingTreeScoringMode::inherit_h21;
    if (value == "reference_h21") return RatingTreeScoringMode::reference_h21;
    if (value == "non_scoring") return RatingTreeScoringMode::non_scoring;
    return std::nullopt;
}

std::string to_string(const RatingTreeScoringMode value) {
    switch (value) {
    case RatingTreeScoringMode::inherit_h21: return "inherit_h21";
    case RatingTreeScoringMode::reference_h21: return "reference_h21";
    case RatingTreeScoringMode::non_scoring: return "non_scoring";
    }
    return "non_scoring";
}

}  // namespace bridge_report::rating_tree
