#pragma once

#include <optional>
#include <vector>

#include "bridge_report/rating_tree/RatingTreeModels.hpp"

namespace bridge_report::rating_tree {

struct RatingTreeCompileResult {
    std::optional<EffectiveRatingTree> tree;
    std::vector<RatingTreeIssue> issues;

    bool ok() const noexcept { return tree.has_value() && issues.empty(); }
};

class RatingTreeCompiler {
public:
    RatingTreeCompileResult compile(
        const standards::StandardPackage& h21_package,
        const standards::StandardPackage* maintenance_package,
        const RatingTreeExtensionPackage& extension) const;
};

}  // namespace bridge_report::rating_tree
