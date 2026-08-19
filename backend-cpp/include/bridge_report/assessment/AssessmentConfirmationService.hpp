#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "bridge_report/assessment/AssessmentService.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"

namespace bridge_report::assessment {

struct AssessmentConfirmationCalculation {
    AssessmentPreview preview;
    std::vector<AssessmentPreviewIssue> issues;
};

AssessmentConfirmationCalculation calculate_assessment_confirmation(
    const standards::TechnicalConditionStandard& evaluator,
    const standards::StandardPackage& package,
    const AssessmentContextSnapshot& context,
    const Json::Value& draft);

// assessment_rule_traces.target_type 使用数据库约束定义的中文枚举。
// 所有评定步骤（包括不进入评分的评定树过滤记录）必须通过这里映射，
// 避免在持久化 SQL 中混入英文内部标识。
std::string assessment_trace_target_type(const std::string& step);

struct AssessmentConfirmationWritten {
    std::string assessment_run_id;
    int component_results{0};
    int part_results{0};
    int control_results{0};
    int rule_traces{0};
    int condition_rating_projections{0};
};

enum class AssessmentConfirmationStatus {
    Completed,
    Blocked,
};

struct AssessmentConfirmationOutcome {
    AssessmentConfirmationStatus status{AssessmentConfirmationStatus::Blocked};
    AssessmentPreview preview;
    AssessmentConfirmationWritten written;
};

class AssessmentConfirmationService {
public:
    AssessmentConfirmationService(
        drogon::orm::DbClientPtr transaction,
        std::shared_ptr<const standards::StandardRegistry> registry);

    // inventory_revision_override：调用方已经按"年度锁定优先、否则最新已确认"解析出的
    // 台账版本。不传则维持原状——读年度锁定的版本，年度没锁就判上下文不完整。
    //
    // 只读预检需要它：年度未锁定时，光靠年度字段这里必然返回
    // assessment_context_incomplete，而预检本身不该为了算一个结果就去写年度。
    // 传入时必须属于同一桥梁且状态为已确认；与年度已锁定的版本不一致时返回
    // component_inventory_revision_changed，既不静默改用它，也不静默忽略它。
    AssessmentConfirmationOutcome calculate(
        const std::string& inspection_year_id,
        const Json::Value& draft,
        const std::optional<std::string>& inventory_revision_override = std::nullopt) const;

    AssessmentConfirmationWritten persist(
        const AssessmentPreview& preview,
        const std::string& target_inspection_year_id,
        const std::string& source_import_record_id,
        const std::string& confirmed_by_user_id) const;

private:
    drogon::orm::DbClientPtr transaction_;
    std::shared_ptr<const standards::StandardRegistry> registry_;
};

}  // namespace bridge_report::assessment
