#pragma once

#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

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

void register_workspace_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
