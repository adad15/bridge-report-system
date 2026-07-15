#include "bridge_report/review/ComponentScore.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace bridge_report::review {

std::optional<ComponentScoreResult> compute_component_score(const std::vector<double>& deductions) {
    if (deductions.empty()) {
        return std::nullopt;
    }
    for (const auto value : deductions) {
        if (value < 0.0 || value > 100.0) {
            return std::nullopt;
        }
    }

    auto ordered = deductions;
    std::sort(ordered.begin(), ordered.end(), std::greater<double>());
    if (ordered.front() == 100.0) {
        return ComponentScoreResult{0.0, std::move(ordered)};
    }

    double total = 0.0;
    for (std::size_t index = 1; index <= ordered.size(); ++index) {
        const auto deduction = ordered[index - 1];
        double u = 0.0;
        if (index == 1) {
            u = deduction;
        } else {
            u = deduction / (100.0 * std::sqrt(static_cast<double>(index))) * (100.0 - total);
        }
        total += u;
    }
    return ComponentScoreResult{100.0 - total, std::move(ordered)};
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
