#pragma once

#include <string>
#include <vector>

#include "bridge_report/standards/AssessmentModels.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"

namespace bridge_report::standards {

class TechnicalConditionStandard : public StandardAlgorithmAdapter {
public:
    ~TechnicalConditionStandard() override = default;

    virtual const StandardManifest& metadata() const noexcept = 0;
    virtual std::vector<AssessmentIssue> validate(const BridgeAssessmentInput& input) const = 0;
    virtual AssessmentOutcome evaluate(const BridgeAssessmentInput& input) const = 0;
    virtual std::string explain(const BridgeAssessmentResult& result) const = 0;
};

}  // namespace bridge_report::standards
