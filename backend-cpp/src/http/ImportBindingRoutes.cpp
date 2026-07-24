#include "bridge_report/http/ImportBindingRoutes.hpp"

#include <string>

#include "bridge_report/db/ImportBindingRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {

Json::Value binding_overview_json(const db::BindingOverview& overview) {
    Json::Value value;
    value["inventory_confirmed"] = overview.inventory_confirmed;
    value["groups"] = Json::Value(Json::arrayValue);
    for (const auto& group : overview.groups) {
        Json::Value group_json;
        group_json["part_name"] = group.part_name;
        group_json["total"] = group.total;
        group_json["bound"] = group.bound;
        group_json["unmatched"] = group.unmatched;
        group_json["ambiguous"] = group.ambiguous;
        group_json["missing"] = group.missing;
        group_json["rows"] = Json::Value(Json::arrayValue);
        for (const auto& row : group.rows) {
            Json::Value row_json;
            row_json["component_number"] = row.component_number;
            row_json["defect_count"] = row.defect_count;
            row_json["status"] = row.status;
            row_json["bridge_component_id"] = row.bridge_component_id.has_value()
                ? Json::Value(*row.bridge_component_id) : Json::Value(Json::nullValue);
            row_json["candidate_component_ids"] = Json::Value(Json::arrayValue);
            for (const auto& candidate : row.candidate_component_ids) {
                row_json["candidate_component_ids"].append(candidate);
            }
            group_json["rows"].append(std::move(row_json));
        }
        value["groups"].append(std::move(group_json));
    }
    return value;
}

namespace {

void respond_binding(const HttpCallback& callback, const db::BindingOutcome& outcome) {
    switch (outcome.status) {
        case db::BindingStatus::Ok: {
            Json::Value body;
            body["overview"] = binding_overview_json(*outcome.overview);
            respond_json(callback, body);
            return;
        }
        case db::BindingStatus::NotFound:
            respond_import_record_not_found(callback);
            return;
        case db::BindingStatus::Conflict: {
            // 批量绑定整批不写，必须让用户知道是哪一条挡住的。
            auto body = make_error_body(
                "component_binding_conflict",
                outcome.rejected_component_number.empty()
                    ? "台账未确认、导入不在待校对阶段，或所选构件类别与部件名称不符。"
                    : "构件 " + outcome.rejected_component_number
                        + " 的类别与部件名称不符，整批未应用。");
            if (!outcome.rejected_component_number.empty()) {
                body["details"]["rejected_component_number"] = outcome.rejected_component_number;
            }
            respond_json(callback, body, drogon::k409Conflict);
            return;
        }
        case db::BindingStatus::Invalid: {
            auto body = make_error_body(
                "invalid_component_binding",
                outcome.rejected_component_number.empty()
                    ? "绑定参数无效或未找到该编号。"
                    : "构件编号 " + outcome.rejected_component_number
                        + " 不在本次导入中，整批未应用。");
            if (!outcome.rejected_component_number.empty()) {
                body["details"]["rejected_component_number"] = outcome.rejected_component_number;
            }
            respond_json(callback, body, drogon::k400BadRequest);
            return;
        }
        default:
            respond_db_unavailable(callback);
            return;
    }
}

// 取回 part_name + component_number（bind 另需 bridge_component_id）。
bool parse_target(const Json::Value* body, std::string& part_name, std::string& number,
                  const HttpCallback& callback) {
    if (body == nullptr || !(*body)["part_name"].isString() || !(*body)["component_number"].isString()) {
        respond_json(callback, make_error_body(
            "invalid_component_binding", "部件名称与构件编号必须是文本。"),
            drogon::k400BadRequest);
        return false;
    }
    part_name = (*body)["part_name"].asString();
    number = (*body)["component_number"].asString();
    if (part_name.empty() || number.empty()) {
        respond_json(callback, make_error_body(
            "invalid_component_binding", "部件名称与构件编号不能为空。"),
            drogon::k400BadRequest);
        return false;
    }
    return true;
}

}  // namespace

void register_import_binding_routes(const drogon::orm::DbClientPtr& db_client) {
    const std::string base = "/api/import-records/{import_id}/component-binding";
    for (const auto& path : {base, base + "/bind", base + "/bind-batch",
                             base + "/mark-missing", base + "/clear"}) {
        register_options_handler(path);
    }

    // 批量绑定：供绑定界面的"批量替换"。单次读改写，任一目标非法则整批不写。
    drogon::app().registerHandler(
        base + "/bind-batch",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback); return;
                }
                const auto body = request->getJsonObject();
                if (body == nullptr || !(*body)["targets"].isArray()
                    || (*body)["targets"].empty()) {
                    respond_json(callback, make_error_body(
                        "invalid_component_binding", "targets 必须是非空数组。"),
                        drogon::k400BadRequest);
                    return;
                }
                std::vector<db::BindingTarget> targets;
                for (const auto& item : (*body)["targets"]) {
                    if (!item.isObject() || !item["part_name"].isString()
                        || !item["component_number"].isString()
                        || !item["bridge_component_id"].isString()) {
                        respond_json(callback, make_error_body(
                            "invalid_component_binding",
                            "每个目标都需要 part_name、component_number 与 bridge_component_id。"),
                            drogon::k400BadRequest);
                        return;
                    }
                    targets.push_back({item["part_name"].asString(),
                                       item["component_number"].asString(),
                                       item["bridge_component_id"].asString()});
                }
                respond_binding(callback,
                    db::ImportBindingRepository(db_client).bind_batch(import_id, targets));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        base,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback); return;
                }
                respond_binding(callback, db::ImportBindingRepository(db_client).overview(import_id));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Get});

    drogon::app().registerHandler(
        base + "/bind",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback); return;
                }
                const auto body = request->getJsonObject();
                std::string part_name, number;
                if (!parse_target(body.get(), part_name, number, callback)) return;
                if (!(*body)["bridge_component_id"].isString()
                    || (*body)["bridge_component_id"].asString().empty()) {
                    respond_json(callback, make_error_body(
                        "invalid_component_binding", "必须选择实际构件。"),
                        drogon::k400BadRequest); return;
                }
                respond_binding(callback, db::ImportBindingRepository(db_client).bind(
                    import_id, part_name, number, (*body)["bridge_component_id"].asString()));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        base + "/mark-missing",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback); return;
                }
                const auto body = request->getJsonObject();
                std::string part_name, number;
                if (!parse_target(body.get(), part_name, number, callback)) return;
                respond_binding(callback, db::ImportBindingRepository(db_client).mark_missing(
                    import_id, part_name, number));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        base + "/clear",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback); return;
                }
                const auto body = request->getJsonObject();
                std::string part_name, number;
                if (!parse_target(body.get(), part_name, number, callback)) return;
                respond_binding(callback, db::ImportBindingRepository(db_client).clear(
                    import_id, part_name, number));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});
}

}  // namespace bridge_report::http
