#pragma once

#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/config/AppConfig.hpp"

namespace bridge_report::http {

enum class WorkspaceResource {
    Bridge,
    InspectionYear,
};

Json::Value workspace_not_found_body(WorkspaceResource resource);
Json::Value inspection_year_already_exists_body(
    int inspection_year,
    const std::string& existing_inspection_year_id
);
bool is_supported_word_source_type(const std::string& source_type);

void register_workspace_routes(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config
);

}  // namespace bridge_report::http
