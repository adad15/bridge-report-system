#pragma once

#include <string>
#include <vector>

#include "bridge_report/standards/LayeredAssessmentModels.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"

namespace bridge_report::standards {

// 分层评定的技术状况标准（JTG/T 5230—2026 起）。与 H21 的 TechnicalConditionStandard 互不继承，
// 现有按 H21 接口取评分器的调用方因此不会误用它。
class LayeredTechnicalConditionStandard : public StandardAlgorithmAdapter {
public:
    ~LayeredTechnicalConditionStandard() override = default;

    virtual const StandardManifest& metadata() const noexcept = 0;
    virtual std::vector<AssessmentIssue> validate(const LayeredAssessmentInput& input) const = 0;
    virtual LayeredAssessmentOutcome evaluate(const LayeredAssessmentInput& input) const = 0;
    virtual std::string explain(const LayeredAssessmentResult& result) const = 0;
};

}  // namespace bridge_report::standards
