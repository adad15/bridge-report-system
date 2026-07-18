#pragma once

#include <memory>

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "bridge_report/standards/StandardRegistry.hpp"
#include "bridge_report/standards/MaintenanceStandard.hpp"

namespace bridge_report::http {

bool parse_standard_enabled_request(const Json::Value& body, bool& enabled);
bool parse_maintenance_query_context_request(
    const Json::Value& body,
    standards::MaintenanceQueryContext& context);
Json::Value periodic_inspection_requirement_json(
    const standards::PeriodicInspectionRequirement& requirement);
Json::Value project_requirement_validation_json(
    const standards::ProjectRequirementValidation& validation);

void register_standard_routes(
    const drogon::orm::DbClientPtr& db_client,
    std::shared_ptr<const standards::StandardRegistry> registry);

}  // namespace bridge_report::http
