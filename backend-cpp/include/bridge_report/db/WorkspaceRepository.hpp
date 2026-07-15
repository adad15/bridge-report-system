#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "bridge_report/review/WorkspaceModels.hpp"

namespace bridge_report::db {

class WorkspaceRepository {
public:
    explicit WorkspaceRepository(drogon::orm::DbClientPtr db_client);

    std::optional<review::BridgeOverview> get_bridge_overview(const std::string& bridge_id);
    std::optional<review::InspectionWorkspace> get_inspection_workspace(const std::string& inspection_year_id);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
