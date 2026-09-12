#include "bridge_report/http/ReportDirectoryRoutes.hpp"

#include <optional>
#include <string>

#include "bridge_report/db/ReportDirectoryRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

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

/// 增删改停用一律管理员（设计 §22）。读取不走这里。
std::optional<db::AuthUser> require_admin(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback) {
    auto user = require_user(db_client, request, callback);
    if (!user.has_value()) return std::nullopt;
    if (!user->is_admin()) {
        respond_forbidden(callback);
        return std::nullopt;
    }
    return user;
}

/// ?only_enabled=true 时只返回启用项。年度配置页选人用它，管理页则要看到停用的。
bool only_enabled_requested(const drogon::HttpRequestPtr& request) {
    const auto value = request->getParameter("only_enabled");
    return value == "true" || value == "1";
}

std::optional<std::string> optional_member(const Json::Value& body, const char* key) {
    if (!body.isMember(key) || body[key].isNull()) return std::nullopt;
    if (!body[key].isString()) return std::nullopt;
    return body[key].asString();
}

/// 必填名称：缺失、非字符串或只有空白都算没填。
bool read_required_name(const Json::Value& body, const char* key, std::string& out) {
    if (!body.isMember(key) || !body[key].isString()) return false;
    const auto value = body[key].asString();
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    const auto last = value.find_last_not_of(" \t\r\n");
    out = value.substr(first, last - first + 1);
    return true;
}

bool parse_personnel_input(const Json::Value& body, report::ReportPersonnelInput& input) {
    if (!body.isObject()) return false;
    if (!read_required_name(body, "full_name", input.full_name)) return false;
    input.organization = optional_member(body, "organization");
    input.job_title = optional_member(body, "job_title");
    input.professional_title = optional_member(body, "professional_title");
    input.qualification_certificate_no =
        optional_member(body, "qualification_certificate_no");
    input.phone = optional_member(body, "phone");
    input.email = optional_member(body, "email");
    input.remarks = optional_member(body, "remarks");
    return true;
}

bool parse_equipment_input(const Json::Value& body, report::ReportEquipmentInput& input) {
    if (!body.isObject()) return false;
    if (!read_required_name(body, "equipment_name", input.equipment_name)) return false;
    input.model_spec = optional_member(body, "model_spec");
    input.asset_number = optional_member(body, "asset_number");
    input.measurement_range = optional_member(body, "measurement_range");
    input.accuracy = optional_member(body, "accuracy");
    input.calibration_certificate_no = optional_member(body, "calibration_certificate_no");
    input.calibration_valid_until = optional_member(body, "calibration_valid_until");
    input.remarks = optional_member(body, "remarks");
    return true;
}

bool parse_enabled_request(const Json::Value* body, bool& enabled) {
    if (body == nullptr || !body->isObject() || !(*body)["is_enabled"].isBool()) return false;
    enabled = (*body)["is_enabled"].asBool();
    return true;
}

void respond_invalid_body(const HttpCallback& callback, const std::string& message) {
    respond_json(callback, make_error_body("report_directory_request_invalid", message),
                 drogon::k400BadRequest);
}

void respond_not_found(const HttpCallback& callback, const std::string& message) {
    respond_json(callback, make_error_body("report_directory_not_found", message),
                 drogon::k404NotFound);
}

/// 被年度配置引用后只能停用，不能硬删除（设计 §15.3）。409 而不是 400：
/// 请求本身没问题，是当前状态不允许。
void respond_referenced(const HttpCallback& callback, const std::string& message) {
    respond_json(callback, make_error_body("report_directory_referenced", message),
                 drogon::k409Conflict);
}

}  // namespace

void register_report_directory_routes(const drogon::orm::DbClientPtr& db_client) {
    const std::string personnel_path = "/api/report/personnel";
    const std::string personnel_item_path = "/api/report/personnel/{id}";
    const std::string personnel_enabled_path = "/api/report/personnel/{id}/enabled";
    const std::string equipment_path = "/api/report/equipment";
    const std::string equipment_item_path = "/api/report/equipment/{id}";
    const std::string equipment_enabled_path = "/api/report/equipment/{id}/enabled";

    for (const auto& path : {personnel_path, personnel_item_path, personnel_enabled_path,
                             equipment_path, equipment_item_path, equipment_enabled_path}) {
        register_options_handler(path);
    }

    // ---------------------------------------------------------------- 人员

    drogon::app().registerHandler(
        personnel_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportDirectoryRepository repository(db_client);
                Json::Value body;
                body["personnel"] = Json::Value(Json::arrayValue);
                for (const auto& person :
                     repository.list_personnel(only_enabled_requested(request))) {
                    body["personnel"].append(person.to_json());
                }
                respond_json(callback, body);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        personnel_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                const auto user = require_admin(db_client, request, callback);
                if (!user.has_value()) return;
                const auto body = request->getJsonObject();
                report::ReportPersonnelInput input;
                if (body == nullptr || !parse_personnel_input(*body, input)) {
                    respond_invalid_body(callback, "请求必须包含非空的 full_name。");
                    return;
                }
                db::ReportDirectoryRepository repository(db_client);
                respond_json(callback, repository.create_personnel(input).to_json(),
                             drogon::k201Created);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        personnel_item_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_not_found(callback, "人员不存在。");
                return;
            }
            try {
                const auto user = require_admin(db_client, request, callback);
                if (!user.has_value()) return;
                const auto body = request->getJsonObject();
                report::ReportPersonnelInput input;
                if (body == nullptr || !parse_personnel_input(*body, input)) {
                    respond_invalid_body(callback, "请求必须包含非空的 full_name。");
                    return;
                }
                db::ReportDirectoryRepository repository(db_client);
                const auto updated = repository.update_personnel(id, input);
                if (!updated.has_value()) {
                    respond_not_found(callback, "人员不存在。");
                    return;
                }
                respond_json(callback, updated->to_json());
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Put});

    drogon::app().registerHandler(
        personnel_enabled_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_not_found(callback, "人员不存在。");
                return;
            }
            try {
                const auto user = require_admin(db_client, request, callback);
                if (!user.has_value()) return;
                bool enabled = false;
                if (!parse_enabled_request(request->getJsonObject().get(), enabled)) {
                    respond_invalid_body(callback, "请求必须包含布尔字段 is_enabled。");
                    return;
                }
                db::ReportDirectoryRepository repository(db_client);
                const auto updated = repository.set_personnel_enabled(id, enabled);
                if (!updated.has_value()) {
                    respond_not_found(callback, "人员不存在。");
                    return;
                }
                respond_json(callback, updated->to_json());
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        personnel_item_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_not_found(callback, "人员不存在。");
                return;
            }
            try {
                const auto user = require_admin(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportDirectoryRepository repository(db_client);
                switch (repository.delete_personnel(id)) {
                    case report::DirectoryDeleteStatus::Deleted: {
                        Json::Value body;
                        body["status"] = "deleted";
                        respond_json(callback, body);
                        return;
                    }
                    case report::DirectoryDeleteStatus::NotFound:
                        respond_not_found(callback, "人员不存在。");
                        return;
                    case report::DirectoryDeleteStatus::Referenced:
                        respond_referenced(callback,
                            "该人员已被年度报告配置引用，不能删除，只能停用。");
                        return;
                }
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Delete});

    // ---------------------------------------------------------------- 设备

    drogon::app().registerHandler(
        equipment_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportDirectoryRepository repository(db_client);
                Json::Value body;
                body["equipment"] = Json::Value(Json::arrayValue);
                for (const auto& item :
                     repository.list_equipment(only_enabled_requested(request))) {
                    body["equipment"].append(item.to_json());
                }
                respond_json(callback, body);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        equipment_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                const auto user = require_admin(db_client, request, callback);
                if (!user.has_value()) return;
                const auto body = request->getJsonObject();
                report::ReportEquipmentInput input;
                if (body == nullptr || !parse_equipment_input(*body, input)) {
                    respond_invalid_body(callback, "请求必须包含非空的 equipment_name。");
                    return;
                }
                db::ReportDirectoryRepository repository(db_client);
                respond_json(callback, repository.create_equipment(input).to_json(),
                             drogon::k201Created);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        equipment_item_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_not_found(callback, "设备不存在。");
                return;
            }
            try {
                const auto user = require_admin(db_client, request, callback);
                if (!user.has_value()) return;
                const auto body = request->getJsonObject();
                report::ReportEquipmentInput input;
                if (body == nullptr || !parse_equipment_input(*body, input)) {
                    respond_invalid_body(callback, "请求必须包含非空的 equipment_name。");
                    return;
                }
                db::ReportDirectoryRepository repository(db_client);
                const auto updated = repository.update_equipment(id, input);
                if (!updated.has_value()) {
                    respond_not_found(callback, "设备不存在。");
                    return;
                }
                respond_json(callback, updated->to_json());
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Put});

    drogon::app().registerHandler(
        equipment_enabled_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_not_found(callback, "设备不存在。");
                return;
            }
            try {
                const auto user = require_admin(db_client, request, callback);
                if (!user.has_value()) return;
                bool enabled = false;
                if (!parse_enabled_request(request->getJsonObject().get(), enabled)) {
                    respond_invalid_body(callback, "请求必须包含布尔字段 is_enabled。");
                    return;
                }
                db::ReportDirectoryRepository repository(db_client);
                const auto updated = repository.set_equipment_enabled(id, enabled);
                if (!updated.has_value()) {
                    respond_not_found(callback, "设备不存在。");
                    return;
                }
                respond_json(callback, updated->to_json());
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        equipment_item_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_not_found(callback, "设备不存在。");
                return;
            }
            try {
                const auto user = require_admin(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportDirectoryRepository repository(db_client);
                switch (repository.delete_equipment(id)) {
                    case report::DirectoryDeleteStatus::Deleted: {
                        Json::Value body;
                        body["status"] = "deleted";
                        respond_json(callback, body);
                        return;
                    }
                    case report::DirectoryDeleteStatus::NotFound:
                        respond_not_found(callback, "设备不存在。");
                        return;
                    case report::DirectoryDeleteStatus::Referenced:
                        respond_referenced(callback,
                            "该设备已被年度报告配置引用，不能删除，只能停用。");
                        return;
                }
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Delete});
}

}  // namespace bridge_report::http
