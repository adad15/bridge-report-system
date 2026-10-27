#pragma once

#include <memory>
#include <string>
#include <vector>

#include "bridge_report/standards/Jtg5230Parameters.hpp"
#include "bridge_report/standards/LayeredTechnicalConditionStandard.hpp"

namespace bridge_report::standards {

class Jtg5230Evaluator;

struct Jtg5230EvaluatorCreation {
    std::unique_ptr<Jtg5230Evaluator> evaluator;  // 有问题时为空
    std::vector<StandardIssue> issues;
};

// JTG/T 5230—2026 评分器：构件 → 桥跨 → 评定单元 → 全桥。纯计算，不碰数据库、文件和时间。
class Jtg5230Evaluator final : public LayeredTechnicalConditionStandard {
public:
    // 规则包声明的算法不对，或参数没通过一致性校验时，不创建评分器。
    static Jtg5230EvaluatorCreation create(const StandardPackage& package);

    std::string algorithm_id() const override;
    const StandardManifest& metadata() const noexcept override;
    std::vector<AssessmentIssue> validate(const LayeredAssessmentInput& input) const override;
    LayeredAssessmentOutcome evaluate(const LayeredAssessmentInput& input) const override;
    std::string explain(const LayeredAssessmentResult& result) const override;

private:
    Jtg5230Evaluator(StandardManifest manifest, Jtg5230Parameters parameters);

    StandardManifest manifest_;
    Jtg5230Parameters parameters_;
};

}  // namespace bridge_report::standards
