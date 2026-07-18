#pragma once

#include <optional>
#include <vector>

#include "bridge_report/standards/TechnicalConditionStandard.hpp"

namespace bridge_report::standards {

struct H21ComponentScoreResult {
    double score{0.0};
    std::vector<double> ordered_deductions;
};

std::optional<H21ComponentScoreResult> compute_h21_component_score(
    const std::vector<double>& deductions);

class H21Evaluator final : public TechnicalConditionStandard {
public:
    explicit H21Evaluator(StandardPackage package);

    std::string algorithm_id() const override;
    const StandardManifest& metadata() const noexcept override;
    std::vector<AssessmentIssue> validate(const BridgeAssessmentInput& input) const override;
    AssessmentOutcome evaluate(const BridgeAssessmentInput& input) const override;
    std::string explain(const BridgeAssessmentResult& result) const override;

    std::optional<int> classify_grade(double score) const;

private:
    StandardPackage package_;
};

}  // namespace bridge_report::standards
