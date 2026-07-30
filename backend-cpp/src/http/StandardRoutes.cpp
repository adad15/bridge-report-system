#include "bridge_report/http/StandardRoutes.hpp"

#include <optional>
#include <utility>

#include "bridge_report/db/StandardRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"
#include "bridge_report/standards/StandardCatalogModels.hpp"
#include "bridge_report/standards/MaintenanceStandard.hpp"

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

Json::Value maintenance_issue_json(const standards::MaintenanceRuleIssue& issue) {
    Json::Value json;
    json["code"] = issue.code;
    json["message"] = issue.message;
    json["field"] = issue.field;
    json["rule_id"] = issue.rule_id;
    return json;
}

Json::Value maintenance_issues_json(
    const std::vector<standards::MaintenanceRuleIssue>& issues) {
    Json::Value json(Json::arrayValue);
    for (const auto& issue : issues) {
        json.append(maintenance_issue_json(issue));
    }
    return json;
}

Json::Value maintenance_sources_json(
    const std::vector<standards::MaintenanceRuleSource>& sources) {
    Json::Value json(Json::arrayValue);
    for (const auto& source : sources) {
        Json::Value item;
        item["rule_id"] = source.rule_id;
        item["source_reference"] = source.source_reference;
        json.append(std::move(item));
    }
    return json;
}

std::unique_ptr<standards::StandardAlgorithmAdapter> resolve_maintenance_algorithm(
    const drogon::orm::DbClientPtr& db_client,
    const std::shared_ptr<const standards::StandardRegistry>& registry,
    const db::AuthUser& user,
    const std::string& package_id,
    const HttpCallback& callback) {
    db::StandardRepository repository(db_client);
    const auto record = repository.find_package_by_id(package_id);
    if (!record.has_value() ||
        (!user.is_admin() && (!record->is_enabled || record->sync_status != "正常"))) {
        respond_json(callback,
                     make_error_body("standard_package_not_found", "规范包不存在。"),
                     drogon::k404NotFound);
        return nullptr;
    }
    if (record->family != standards::StandardFamily::maintenance) {
        respond_json(
            callback,
            make_error_body("maintenance_standard_required", "所选规范包不是养护规范。"),
            drogon::k409Conflict);
        return nullptr;
    }
    const standards::StandardPackageKey key{
        record->family, record->standard_id, record->package_version};
    const auto* package = registry->find(key);
    if (package == nullptr || package->manifest.content_checksum != record->content_checksum) {
        respond_json(
            callback,
            make_error_body("standard_package_unavailable", "规范包暂不可用，请联系管理员。"),
            drogon::k409Conflict);
        return nullptr;
    }
    auto algorithm = registry->create_algorithm(key);
    if (algorithm == nullptr ||
        dynamic_cast<standards::MaintenanceStandard*>(algorithm.get()) == nullptr) {
        respond_json(
            callback,
            make_error_body(
                "maintenance_adapter_unavailable", "养护规范查询适配器暂不可用。"),
            drogon::k409Conflict);
        return nullptr;
    }
    return algorithm;
}

}  // namespace

bool parse_standard_enabled_request(const Json::Value& body, bool& enabled) {
    if (!body.isObject() || !body.isMember("enabled") || !body["enabled"].isBool()) {
        return false;
    }
    enabled = body["enabled"].asBool();
    return true;
}

bool parse_maintenance_query_context_request(
    const Json::Value& body,
    standards::MaintenanceQueryContext& context) {
    if (!body.isObject()) {
        return false;
    }
    context = {};
    if (body.isMember("maintenance_level_id")) {
        if (!body["maintenance_level_id"].isString()) {
            return false;
        }
        context.maintenance_level_id = body["maintenance_level_id"].asString();
    }
    if (body.isMember("inspection_type_id")) {
        if (!body["inspection_type_id"].isString()) {
            return false;
        }
        context.inspection_type_id = body["inspection_type_id"].asString();
    }
    if (body.isMember("planned_interval_years")) {
        if (!body["planned_interval_years"].isNumeric()) {
            return false;
        }
        context.planned_interval_years = body["planned_interval_years"].asDouble();
    }
    return true;
}

Json::Value periodic_inspection_requirement_json(
    const standards::PeriodicInspectionRequirement& requirement) {
    Json::Value json;
    json["standard_id"] = requirement.standard_id;
    json["package_version"] = requirement.package_version;
    json["maintenance_level_id"] = requirement.maintenance_level_id;
    json["inspection_type_id"] = requirement.inspection_type_id;
    json["maximum_interval_years"] = requirement.maximum_interval_years;
    json["content_groups"] = Json::Value(Json::arrayValue);
    for (const auto& group : requirement.content_groups) {
        Json::Value item;
        item["id"] = group.id;
        item["name"] = group.name;
        json["content_groups"].append(std::move(item));
    }
    json["sources"] = maintenance_sources_json(requirement.sources);
    return json;
}

Json::Value project_requirement_validation_json(
    const standards::ProjectRequirementValidation& validation) {
    Json::Value json;
    json["valid"] = validation.valid;
    json["issues"] = maintenance_issues_json(validation.issues);
    json["sources"] = maintenance_sources_json(validation.sources);
    return json;
}

void register_standard_routes(
    const drogon::orm::DbClientPtr& db_client,
    std::shared_ptr<const standards::StandardRegistry> registry) {
    const std::string list_path = "/api/standards";
    const std::string catalog_path = "/api/standards/{package_id}/catalog";
    const std::string mapping_catalogs_path = "/api/standards/technical-mapping-catalogs";
    const std::string enabled_path = "/api/standards/{package_id}/enabled";
    const std::string periodic_requirements_path =
        "/api/standards/{package_id}/maintenance/periodic-requirements";
    const std::string validate_requirements_path =
        "/api/standards/{package_id}/maintenance/validate-project-requirements";
    register_options_handler(list_path);
    register_options_handler(catalog_path);
    register_options_handler(mapping_catalogs_path);
    register_options_handler(enabled_path);
    register_options_handler(periodic_requirements_path);
    register_options_handler(validate_requirements_path);

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
        mapping_catalogs_path,
        [db_client, registry](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::StandardRepository repository(db_client);
                Json::Value body;
                body["catalogs"] = Json::Value(Json::arrayValue);
                for (const auto& record : repository.list_packages(!user->is_admin())) {
                    if (record.family != standards::StandardFamily::technical_condition ||
                        !record.is_enabled || record.sync_status != "正常")
                        continue;
                    const standards::StandardPackageKey key{
                        record.family, record.standard_id, record.package_version};
                    const auto* package = registry->find(key);
                    if (package == nullptr ||
                        package->manifest.content_checksum != record.content_checksum)
                        continue;
                    body["catalogs"].append(
                        standards::standard_mapping_catalog_json(record, *package));
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

    drogon::app().registerHandler(
        periodic_requirements_path,
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
                const auto body = request->getJsonObject();
                standards::MaintenanceQueryContext context;
                if (body == nullptr ||
                    !parse_maintenance_query_context_request(*body, context)) {
                    respond_json(
                        callback,
                        make_error_body(
                            "maintenance_context_invalid", "养护规则查询上下文格式无效。"),
                        drogon::k400BadRequest);
                    return;
                }
                auto algorithm = resolve_maintenance_algorithm(
                    db_client, registry, *user, package_id, callback);
                if (algorithm == nullptr) return;
                const auto* maintenance =
                    dynamic_cast<standards::MaintenanceStandard*>(algorithm.get());
                const auto result = maintenance->periodic_inspection_requirements(context);
                if (!result.ok()) {
                    auto error = make_error_body(
                        "maintenance_query_rejected", "无法查询定期检查要求。");
                    error["issues"] = maintenance_issues_json(result.issues);
                    respond_json(callback, error, drogon::k400BadRequest);
                    return;
                }
                Json::Value response;
                response["requirement"] = periodic_inspection_requirement_json(*result.value);
                respond_json(callback, response);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        validate_requirements_path,
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
                const auto body = request->getJsonObject();
                standards::MaintenanceQueryContext context;
                if (body == nullptr ||
                    !parse_maintenance_query_context_request(*body, context)) {
                    respond_json(
                        callback,
                        make_error_body(
                            "maintenance_context_invalid", "养护规则查询上下文格式无效。"),
                        drogon::k400BadRequest);
                    return;
                }
                auto algorithm = resolve_maintenance_algorithm(
                    db_client, registry, *user, package_id, callback);
                if (algorithm == nullptr) return;
                const auto* maintenance =
                    dynamic_cast<standards::MaintenanceStandard*>(algorithm.get());
                const auto validation = maintenance->validate_project_requirements(context);
                Json::Value response;
                response["validation"] = project_requirement_validation_json(validation);
                respond_json(callback, response);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});
}

}  // namespace bridge_report::http
