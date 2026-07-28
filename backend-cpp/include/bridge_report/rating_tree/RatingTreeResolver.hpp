#pragma once

#include <optional>
#include <string>
#include <vector>

#include "bridge_report/rating_tree/RatingTreeModels.hpp"

namespace bridge_report::rating_tree {

enum class RatingTreeResolutionStatus {
    resolved,
    ambiguous,
    candidates,
    not_found,
};

struct RatingTreeResolution {
    RatingTreeResolutionStatus status{RatingTreeResolutionStatus::not_found};
    std::optional<std::string> node_id;
    std::string match_method;
    std::vector<std::string> candidate_node_ids;
    std::optional<std::string> h21_indicator_id;
    std::vector<int> allowed_scales;
    std::string match_evidence;
};

class RatingTreeResolver {
public:
    RatingTreeResolution resolve(
        const EffectiveRatingTree& tree,
        const std::string& bridge_type_id,
        const std::string& component_category_id,
        const std::string& original_defect_name) const;
};

}  // namespace bridge_report::rating_tree
