#pragma once

#include <memory>
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

    AssessmentConfirmationOutcome calculate(
        const std::string& inspection_year_id,
        const Json::Value& draft) const;

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
