#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "bridge_report/db/AuthRepository.hpp"
#include "bridge_report/rating_tree/RatingTreeModels.hpp"
#include "bridge_report/standards/AssessmentModels.hpp"
#include "bridge_report/standards/StandardModels.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"
#include "bridge_report/standards/TechnicalConditionStandard.hpp"

namespace bridge_report::assessment {

struct AssessmentPreviewPayload {
    Json::Value draft{Json::objectValue};
    int client_revision{0};
};

struct AssessmentComponentSnapshot {
    std::string component_instance_id;
    std::string component_type_id;
};

struct AssessmentPreviewIssue {
    std::string code;
    std::string message;
    std::string entity_type;
    std::string entity_id;
    std::string field_path;
    std::string rule_id;

    Json::Value to_json() const;
};

struct AssessmentContextSnapshot {
    std::string inspection_year_id;
    std::string standard_package_id;
    std::string standard_profile_id;
    std::string inventory_revision_id;
    std::string rating_tree_version_id;
    std::string rating_tree_content_checksum;
    std::optional<rating_tree::EffectiveRatingTree> rating_tree;
    bool inventory_confirmed{false};
    std::string bridge_type_id;
    std::vector<AssessmentComponentSnapshot> components;
    std::vector<AssessmentPreviewIssue> issues;
};

struct AssessmentPreview {
    int client_revision{0};
    std::string input_checksum;
    Json::Value input_summary{Json::objectValue};
    Json::Value standard_identity{Json::objectValue};
    std::optional<standards::BridgeAssessmentResult> result;
    std::vector<AssessmentPreviewIssue> issues;
    std::optional<std::string> assessment_run_id;

    Json::Value to_json() const;
};

AssessmentPreview calculate_assessment_preview(
    const standards::TechnicalConditionStandard& evaluator,
    const standards::StandardPackage& package,
    const AssessmentContextSnapshot& context,
    const Json::Value& draft,
    int client_revision);

Json::Value assessment_result_to_json(
    const standards::BridgeAssessmentResult& result);

enum class AssessmentServiceStatus {
    Completed,
    Blocked,
    NotFound,
    LockRejected,
};

struct AssessmentServiceOutcome {
    AssessmentServiceStatus status{AssessmentServiceStatus::Blocked};
    AssessmentPreview preview;
};

class AssessmentService {
public:
    AssessmentService(
        drogon::orm::DbClientPtr db_client,
        std::shared_ptr<const standards::StandardRegistry> registry);

    AssessmentServiceOutcome preview(
        const std::string& import_record_id,
        const AssessmentPreviewPayload& payload,
        const db::AuthUser& user,
        const std::string& edit_lock_token) const;

private:
    drogon::orm::DbClientPtr db_client_;
    std::shared_ptr<const standards::StandardRegistry> registry_;
};

}  // namespace bridge_report::assessment
