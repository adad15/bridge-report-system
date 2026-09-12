#include "bridge_report/http/InspectionReportSettingsRoutes.hpp"

#include <optional>
#include <string>

#include "bridge_report/db/InspectionReportSettingsRepository.hpp"
#include "bridge_report/db/ReportPreflightRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {
namespace {

std::optional<db::AuthUser> require_user(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback) {
    auto user = authenticate_request(db_client, request);
    if (!user.has_value()) respond_unauthorized(callback);
    return user;
}

void respond_invalid(const HttpCallback& callback, const std::string& message) {
    respond_json(callback, make_error_body("report_settings_request_invalid", message),
                 drogon::k400BadRequest);
}

/// 把保存结果翻成响应。每一种都要能让用户知道该改哪里，不能一律"保存失败"。
bool respond_write_status(const HttpCallback& callback, report::SettingsWriteStatus status) {
    switch (status) {
        case report::SettingsWriteStatus::Ok:
            return false;
        case report::SettingsWriteStatus::YearNotFound:
            respond_json(callback,
                make_error_body("inspection_year_not_found", "年度检查不存在。"),
                drogon::k404NotFound);
            return true;
        case report::SettingsWriteStatus::TemplateNotFound:
            respond_json(callback,
                make_error_body("report_template_not_found", "所选模板不存在。"),
                drogon::k404NotFound);
            return true;
        case report::SettingsWriteStatus::TemplateNotUsable:
            respond_json(callback,
                make_error_body("report_template_not_usable",
                    "所选模板已停用或未通过契约校验，请重新选择。"),
                drogon::k409Conflict);
            return true;
        case report::SettingsWriteStatus::PersonnelNotFound:
            respond_json(callback,
                make_error_body("report_personnel_not_found", "所选人员不存在。"),
                drogon::k404NotFound);
            return true;
        case report::SettingsWriteStatus::PersonnelDisabled:
            respond_json(callback,
                make_error_body("report_personnel_disabled",
                    "所选人员已停用，不能加入新的报告配置。"),
                drogon::k409Conflict);
            return true;
        case report::SettingsWriteStatus::EquipmentNotFound:
            respond_json(callback,
                make_error_body("report_equipment_not_found", "所选设备不存在。"),
                drogon::k404NotFound);
            return true;
        case report::SettingsWriteStatus::EquipmentDisabled:
            respond_json(callback,
                make_error_body("report_equipment_disabled",
                    "所选设备已停用，不能加入新的报告配置。"),
                drogon::k409Conflict);
            return true;
        case report::SettingsWriteStatus::ComparisonInvalid:
            respond_json(callback,
                make_error_body("report_comparison_invalid",
                    "所选历史对比检查不可用：必须是同一座桥、当前修订、已确认或已归档、"
                    "且年度早于本次检查。请重新选择。"),
                drogon::k409Conflict);
            return true;
        case report::SettingsWriteStatus::Failed:
            respond_db_unavailable(callback);
            return true;
    }
    respond_db_unavailable(callback);
    return true;
}

std::optional<std::string> optional_uuid_member(const Json::Value& body, const char* key) {
    if (!body.isMember(key) || body[key].isNull()) return std::nullopt;
    if (!body[key].isString()) return std::nullopt;
    const auto value = body[key].asString();
    if (value.empty()) return std::nullopt;
    return value;
}

bool parse_settings_input(
    const Json::Value& body,
    report::InspectionReportSettingsInput& input,
    std::string& message) {
    if (!body.isObject()) {
        message = "请求体必须是 JSON 对象。";
        return false;
    }
    input.template_id = optional_uuid_member(body, "template_id");
    input.comparison_inspection_id = optional_uuid_member(body, "comparison_inspection_id");
    if (input.template_id.has_value() && !is_valid_uuid(*input.template_id)) {
        message = "template_id 不是合法的 UUID。";
        return false;
    }
    if (input.comparison_inspection_id.has_value() &&
        !is_valid_uuid(*input.comparison_inspection_id)) {
        message = "comparison_inspection_id 不是合法的 UUID。";
        return false;
    }

    if (body.isMember("personnel")) {
        if (!body["personnel"].isArray()) {
            message = "personnel 必须是数组。";
            return false;
        }
        for (const auto& entry : body["personnel"]) {
            if (!entry.isObject() || !entry["personnel_id"].isString() ||
                !entry["role_code"].isString()) {
                message = "personnel 的每一项都必须含 personnel_id 与 role_code。";
                return false;
            }
            report::PersonnelAssignmentInput item;
            item.personnel_id = entry["personnel_id"].asString();
            item.role_code = entry["role_code"].asString();
            if (!is_valid_uuid(item.personnel_id)) {
                message = "personnel_id 不是合法的 UUID。";
                return false;
            }
            item.sort_order = entry["sort_order"].isInt() ? entry["sort_order"].asInt() : 0;
            if (item.sort_order < 0) item.sort_order = 0;
            input.personnel.push_back(std::move(item));
        }
    }

    if (body.isMember("equipment")) {
        if (!body["equipment"].isArray()) {
            message = "equipment 必须是数组。";
            return false;
        }
        for (const auto& entry : body["equipment"]) {
            if (!entry.isObject() || !entry["equipment_id"].isString()) {
                message = "equipment 的每一项都必须含 equipment_id。";
                return false;
            }
            report::EquipmentAssignmentInput item;
            item.equipment_id = entry["equipment_id"].asString();
            if (!is_valid_uuid(item.equipment_id)) {
                message = "equipment_id 不是合法的 UUID。";
                return false;
            }
            if (entry["purpose"].isString()) item.purpose = entry["purpose"].asString();
            item.sort_order = entry["sort_order"].isInt() ? entry["sort_order"].asInt() : 0;
            if (item.sort_order < 0) item.sort_order = 0;
            input.equipment.push_back(std::move(item));
        }
    }
    return true;
}

}  // namespace

void register_inspection_report_settings_routes(const drogon::orm::DbClientPtr& db_client) {
    const std::string settings_path = "/api/inspection-years/{id}/report-settings";
    const std::string candidates_path =
        "/api/inspection-years/{id}/report-settings/comparison-candidates";
    const std::string preflight_path = "/api/inspection-years/{id}/report-preflight";

    for (const auto& path : {settings_path, candidates_path, preflight_path}) {
        register_options_handler(path);
    }

    drogon::app().registerHandler(
        settings_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_json(callback,
                    make_error_body("inspection_year_not_found", "年度检查不存在。"),
                    drogon::k404NotFound);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::InspectionReportSettingsRepository repository(db_client);
                const auto settings = repository.find(id);
                if (!settings.has_value()) {
                    respond_json(callback,
                        make_error_body("inspection_year_not_found", "年度检查不存在。"),
                        drogon::k404NotFound);
                    return;
                }
                respond_json(callback, settings->to_json());
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        candidates_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_json(callback,
                    make_error_body("inspection_year_not_found", "年度检查不存在。"),
                    drogon::k404NotFound);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::InspectionReportSettingsRepository repository(db_client);
                Json::Value body;
                body["candidates"] = Json::Value(Json::arrayValue);
                for (const auto& candidate : repository.list_comparison_candidates(id)) {
                    body["candidates"].append(candidate.to_json());
                }
                respond_json(callback, body);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    // 生成前检查：把"点了生成才发现不行"提前到生成页上（设计 §16、§21.4）。
    drogon::app().registerHandler(
        preflight_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_json(callback,
                    make_error_body("inspection_year_not_found", "年度检查不存在。"),
                    drogon::k404NotFound);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportPreflightRepository repository(db_client);
                const auto result = repository.evaluate(id);
                if (!result.has_value()) {
                    respond_json(callback,
                        make_error_body("inspection_year_not_found", "年度检查不存在。"),
                        drogon::k404NotFound);
                    return;
                }
                // 检查不通过是正常结果，走 200 带明细——用户要看到每一条阻断项，
                // 而不是一句 409。
                respond_json(callback, result->to_json());
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        settings_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_json(callback,
                    make_error_body("inspection_year_not_found", "年度检查不存在。"),
                    drogon::k404NotFound);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                const auto body = request->getJsonObject();
                report::InspectionReportSettingsInput input;
                std::string message;
                if (body == nullptr || !parse_settings_input(*body, input, message)) {
                    respond_invalid(callback, message.empty() ? "请求体不合法。" : message);
                    return;
                }
                input.configured_by_user_id = user->id;

                db::InspectionReportSettingsRepository repository(db_client);
                if (respond_write_status(callback, repository.save(id, input))) return;
                const auto saved = repository.find(id);
                respond_json(callback, saved.has_value() ? saved->to_json()
                                                         : Json::Value(Json::objectValue));
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Put});
}

}  // namespace bridge_report::http
