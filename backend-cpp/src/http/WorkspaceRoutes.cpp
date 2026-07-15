#include "bridge_report/http/WorkspaceRoutes.hpp"

#include <string>

#include <drogon/drogon.h>

#include "bridge_report/db/WorkspaceRepository.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {
namespace {

void respond_workspace_not_found(const HttpCallback& callback, const WorkspaceResource resource) {
    respond_json(callback, workspace_not_found_body(resource), drogon::k404NotFound);
}

}  // namespace

Json::Value workspace_not_found_body(const WorkspaceResource resource) {
    if (resource == WorkspaceResource::Bridge) {
        return make_error_body("bridge_not_found", "桥梁不存在。");
    }
    return make_error_body("inspection_year_not_found", "年度检测不存在。");
}

void register_workspace_routes(const drogon::orm::DbClientPtr& db_client) {
    const std::string bridge_path = "/api/bridges/{bridge_id}/overview";
    const std::string inspection_path = "/api/inspection-years/{inspection_year_id}/workspace";
    register_options_handler(bridge_path);
    register_options_handler(inspection_path);

    drogon::app().registerHandler(
        bridge_path,
        [db_client](const drogon::HttpRequestPtr&, HttpCallback&& callback, const std::string& bridge_id) {
            if (!is_valid_uuid(bridge_id)) {
                respond_workspace_not_found(callback, WorkspaceResource::Bridge);
                return;
            }
            try {
                db::WorkspaceRepository repository(db_client);
                const auto overview = repository.get_bridge_overview(bridge_id);
                if (!overview.has_value()) {
                    respond_workspace_not_found(callback, WorkspaceResource::Bridge);
                    return;
                }
                respond_json(callback, overview->to_json());
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        inspection_path,
        [db_client](const drogon::HttpRequestPtr&, HttpCallback&& callback,
                    const std::string& inspection_year_id) {
            if (!is_valid_uuid(inspection_year_id)) {
                respond_workspace_not_found(callback, WorkspaceResource::InspectionYear);
                return;
            }
            try {
                db::WorkspaceRepository repository(db_client);
                const auto workspace = repository.get_inspection_workspace(inspection_year_id);
                if (!workspace.has_value()) {
                    respond_workspace_not_found(callback, WorkspaceResource::InspectionYear);
                    return;
                }
                respond_json(callback, workspace->to_json());
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );
}

}  // namespace bridge_report::http
