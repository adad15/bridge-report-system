#include "bridge_report/http/WorkspaceRoutes.hpp"

#include <string>

#include <drogon/drogon.h>

#include "bridge_report/db/WorkspaceRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
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

Json::Value inspection_year_already_exists_body(
    const int inspection_year,
    const std::string& existing_inspection_year_id
) {
    auto body = make_error_body(
        "inspection_year_already_exists",
        "该桥梁已经存在 " + std::to_string(inspection_year) + " 年度检测。"
    );
    body["existing_inspection_year_id"] = existing_inspection_year_id;
    return body;
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

    // 该路径的 GET 与 OPTIONS 已由年度列表路由注册，这里只追加创建动作。
    drogon::app().registerHandler(
        "/api/bridges/{bridge_id}/inspection-years",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& bridge_id) {
            if (!is_valid_uuid(bridge_id)) {
                respond_workspace_not_found(callback, WorkspaceResource::Bridge);
                return;
            }
            const auto request_body = request->getJsonObject();
            if (request_body == nullptr) {
                respond_json(callback, make_error_body("invalid_json_body", "请求体不是合法的 JSON。"),
                             drogon::k400BadRequest);
                return;
            }
            if (!request_body->isMember("inspection_year")
                || !(*request_body)["inspection_year"].isInt()) {
                respond_json(callback,
                             make_error_body("invalid_inspection_year", "检测年度必须是 1900 至 2200 的整数。"),
                             drogon::k400BadRequest);
                return;
            }
            const int inspection_year = (*request_body)["inspection_year"].asInt();
            if (inspection_year < 1900 || inspection_year > 2200) {
                respond_json(callback,
                             make_error_body("invalid_inspection_year", "检测年度必须是 1900 至 2200 的整数。"),
                             drogon::k400BadRequest);
                return;
            }

            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback);
                    return;
                }
                db::WorkspaceRepository repository(db_client);
                const auto outcome = repository.create_inspection_year(bridge_id, inspection_year);
                if (outcome.status == db::CreateInspectionYearStatus::BridgeNotFound) {
                    respond_workspace_not_found(callback, WorkspaceResource::Bridge);
                    return;
                }
                if (outcome.status == db::CreateInspectionYearStatus::AlreadyExists) {
                    respond_json(
                        callback,
                        inspection_year_already_exists_body(
                            inspection_year, *outcome.existing_inspection_year_id),
                        drogon::k409Conflict
                    );
                    return;
                }

                Json::Value body;
                body["inspection_year"] = outcome.inspection_year->to_json();
                respond_json(callback, body, drogon::k201Created);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );
}

}  // namespace bridge_report::http
