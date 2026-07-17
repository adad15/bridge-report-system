#include "bridge_report/http/StandardRoutes.hpp"

#include <optional>
#include <utility>

#include "bridge_report/db/StandardRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"
#include "bridge_report/standards/StandardCatalogModels.hpp"

namespace bridge_report::http {
namespace {

std::optional<db::AuthUser> require_user(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback) {
    auto user = authenticate_request(db_client, request);
    if (!user.has_value()) {
        respond_unauthorized(callback);
    }
    return user;
}

}  // namespace

bool parse_standard_enabled_request(const Json::Value& body, bool& enabled) {
    if (!body.isObject() || !body.isMember("enabled") || !body["enabled"].isBool()) {
        return false;
    }
    enabled = body["enabled"].asBool();
    return true;
}

void register_standard_routes(
    const drogon::orm::DbClientPtr& db_client,
    std::shared_ptr<const standards::StandardRegistry> registry) {
    const std::string list_path = "/api/standards";
    const std::string catalog_path = "/api/standards/{package_id}/catalog";
    const std::string enabled_path = "/api/standards/{package_id}/enabled";
    register_options_handler(list_path);
    register_options_handler(catalog_path);
    register_options_handler(enabled_path);

    drogon::app().registerHandler(
        list_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::StandardRepository repository(db_client);
                Json::Value body;
                body["packages"] = Json::Value(Json::arrayValue);
                for (const auto& package : repository.list_packages(!user->is_admin())) {
                    body["packages"].append(standards::standard_package_summary_json(package));
                }
                respond_json(callback, body);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        catalog_path,
        [db_client, registry](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                              const std::string& package_id) {
            if (!is_valid_uuid(package_id)) {
                respond_json(callback,
                             make_error_body("standard_package_not_found", "规范包不存在。"),
                             drogon::k404NotFound);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::StandardRepository repository(db_client);
                const auto record = repository.find_package_by_id(package_id);
                if (!record.has_value() ||
                    (!user->is_admin() && (!record->is_enabled || record->sync_status != "正常"))) {
                    respond_json(callback,
                                 make_error_body("standard_package_not_found", "规范包不存在。"),
                                 drogon::k404NotFound);
                    return;
                }
                const standards::StandardPackageKey key{
                    record->family, record->standard_id, record->package_version};
                const auto* package = registry->find(key);
                if (package == nullptr ||
                    package->manifest.content_checksum != record->content_checksum) {
                    respond_json(
                        callback,
                        make_error_body(
                            "standard_package_unavailable", "规范包目录暂不可用，请联系管理员。"),
                        drogon::k409Conflict);
                    return;
                }
                respond_json(callback, standards::standard_catalog_json(*record, *package));
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        enabled_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& package_id) {
            if (!is_valid_uuid(package_id)) {
                respond_json(callback,
                             make_error_body("standard_package_not_found", "规范包不存在。"),
                             drogon::k404NotFound);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                if (!user->is_admin()) {
                    respond_forbidden(callback);
                    return;
                }
                const auto body = request->getJsonObject();
                bool enabled = false;
                if (body == nullptr || !parse_standard_enabled_request(*body, enabled)) {
                    respond_json(
                        callback,
                        make_error_body("invalid_standard_enabled", "enabled 必须是布尔值。"),
                        drogon::k400BadRequest);
                    return;
                }
                db::StandardRepository repository(db_client);
                const auto status = repository.set_package_enabled(package_id, enabled, user->role);
                if (status == db::SetStandardPackageEnabledStatus::NotFound) {
                    respond_json(callback,
                                 make_error_body("standard_package_not_found", "规范包不存在。"),
                                 drogon::k404NotFound);
                    return;
                }
                if (status == db::SetStandardPackageEnabledStatus::FaultBlocked) {
                    respond_json(
                        callback,
                        make_error_body(
                            "standard_package_fault_blocked", "故障规范包不能启用，请先修复规则包。"),
                        drogon::k409Conflict);
                    return;
                }
                const auto updated = repository.find_package_by_id(package_id);
                Json::Value response;
                response["package"] = standards::standard_package_summary_json(*updated);
                respond_json(callback, response);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Patch});
}

}  // namespace bridge_report::http
