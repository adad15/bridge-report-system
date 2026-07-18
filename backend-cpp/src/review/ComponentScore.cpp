#include "bridge_report/review/ComponentScore.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

#include "bridge_report/standards/H21Evaluator.hpp"

namespace bridge_report::review {

std::optional<ComponentScoreResult> compute_component_score(const std::vector<double>& deductions) {
    const auto result = standards::compute_h21_component_score(deductions);
    if (!result.has_value()) {
        return std::nullopt;
    }
    return ComponentScoreResult{result->score, result->ordered_deductions};
}

double round_score_to_two_decimals(double value) {
    if (value < 0.0) {
        return -std::floor(-value * 100.0 + 0.5) / 100.0;
    }
    return std::floor(value * 100.0 + 0.5) / 100.0;
}

std::string classify_score_validation(
    std::optional<double> source_score,
    std::optional<double> calculated_score
) {
    if (!calculated_score.has_value()) {
        return "无法复算";
    }
    if (!source_score.has_value()) {
        return "不一致";
    }
    if (round_score_to_two_decimals(*source_score) == round_score_to_two_decimals(*calculated_score)) {
        return "一致";
    }
    return "不一致";
}

}  // namespace bridge_report::review
