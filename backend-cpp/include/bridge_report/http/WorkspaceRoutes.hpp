#pragma once

#include <drogon/orm/DbClient.h>
#include <json/value.h>

namespace bridge_report::http {

enum class WorkspaceResource {
    Bridge,
    InspectionYear,
};

Json::Value workspace_not_found_body(WorkspaceResource resource);

void register_workspace_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
