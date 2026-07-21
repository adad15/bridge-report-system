#include "bridge_report/http/BridgeAdministrationRoutes.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

#include "bridge_report/db/BridgeDeletionRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {
namespace {

std::string trim(std::string value) {
    const auto nonspace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), nonspace));
    value.erase(std::find_if(value.rbegin(), value.rend(), nonspace).base(), value.end());
    return value;
}

bool optional_text(
    const Json::Value& body, const char* key, const std::size_t max_length,
    std::optional<std::string>& output
) {
    if (!body.isMember(key) || body[key].isNull()) { output.reset(); return true; }
    if (!body[key].isString()) return false;
    auto value = trim(body[key].asString());
    if (value.size() > max_length) return false;
    output = value.empty() ? std::nullopt : std::optional<std::string>(std::move(value));
    return true;
}

Json::Value bridge_summary_json(const db::BridgeAdministrationSummary& bridge) {
    Json::Value json;
    json["id"] = bridge.id;
    json["system_number"] = bridge.system_number;
    json["bridge_name"] = bridge.bridge_name;
    json["route_number"] = bridge.route_number ? Json::Value(*bridge.route_number) : Json::Value(Json::nullValue);
    json["route_name"] = bridge.route_name ? Json::Value(*bridge.route_name) : Json::Value(Json::nullValue);
    json["administrative_region"] = bridge.administrative_region ? Json::Value(*bridge.administrative_region) : Json::Value(Json::nullValue);
    json["station_mark"] = bridge.station_mark ? Json::Value(*bridge.station_mark) : Json::Value(Json::nullValue);
    json["status"] = bridge.status;
    return json;
}

bool require_admin(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::optional<db::AuthUser>& user
) {
    user = authenticate_request(db_client, request);
    if (!user) { respond_unauthorized(callback); return false; }
    if (!user->is_admin()) { respond_forbidden(callback); return false; }
    return true;
}

}  // namespace

std::optional<std::string> parse_create_bridge_request(
    const Json::Value& body,
    db::CreateBridgeRequest& output
) {
    if (!body.isObject()) return "invalid_json_body";
    if (!body["bridge_name"].isString()) return "bridge_name_required";
    output.bridge_name = trim(body["bridge_name"].asString());
    if (output.bridge_name.empty() || output.bridge_name.size() > 200) return "invalid_bridge_name";
    if (!optional_text(body, "route_number", 100, output.route_number) ||
        !optional_text(body, "route_name", 200, output.route_name) ||
        !optional_text(body, "administrative_region", 200, output.administrative_region) ||
        !optional_text(body, "station_mark", 100, output.station_mark)) {
        return "invalid_bridge_field";
    }
    output.status = body.isMember("status") && body["status"].isString()
        ? trim(body["status"].asString()) : "在用";
    if (output.status != "在用" && output.status != "停用" && output.status != "拆除")
        return "invalid_bridge_status";
    return std::nullopt;
}

std::optional<std::string> parse_bridge_selection(
    const Json::Value& body,
    std::vector<std::string>& bridge_ids
) {
    if (!body.isObject() || !body["bridge_ids"].isArray()) return "invalid_bridge_selection";
    if (body["bridge_ids"].empty() || body["bridge_ids"].size() > 100) return "invalid_bridge_selection";
    std::set<std::string> seen;
    for (const auto& value : body["bridge_ids"]) {
        if (!value.isString() || !is_valid_uuid(value.asString()) || !seen.insert(value.asString()).second)
            return "invalid_bridge_selection";
        bridge_ids.push_back(value.asString());
    }
    return std::nullopt;
}

std::optional<std::string> parse_delete_bridges_request(
    const Json::Value& body,
    DeleteBridgesRequest& output
) {
    if (!body.isObject() || !body["reason"].isString() ||
        !body["confirmation_text"].isString() || !body["items"].isArray())
        return "invalid_bridge_deletion_request";
    output.reason = trim(body["reason"].asString());
    output.confirmation_text = body["confirmation_text"].asString();
    if (output.reason.empty() || output.reason.size() > 4000 || body["items"].empty() ||
        body["items"].size() > 100) return "invalid_bridge_deletion_request";
    std::set<std::string> seen;
    for (const auto& value : body["items"]) {
        if (!value.isObject() || !value["bridge_id"].isString() ||
            !value["impact_token"].isString()) return "invalid_bridge_deletion_request";
        DeleteBridgeItemRequest item{value["bridge_id"].asString(), trim(value["impact_token"].asString())};
        if (!is_valid_uuid(item.bridge_id) || item.impact_token.empty() ||
            !seen.insert(item.bridge_id).second) return "invalid_bridge_deletion_request";
        output.items.push_back(std::move(item));
    }
    return std::nullopt;
}

void register_bridge_administration_routes(
    const drogon::orm::DbClientPtr& db_client,
    const std::shared_ptr<deletion::ArchiveFileCleanupCoordinator>& cleanup_coordinator
) {
    register_options_handler("/api/bridges/deletion-impact");

    drogon::app().registerHandler(
        "/api/bridges",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
            std::optional<db::AuthUser> user;
            if (!require_admin(db_client, request, callback, user)) return;
            const auto body = request->getJsonObject();
            db::CreateBridgeRequest parsed;
            if (!body) { respond_json(callback, make_error_body("invalid_json_body", "请求体不是合法 JSON。"), drogon::k400BadRequest); return; }
            if (const auto error = parse_create_bridge_request(*body, parsed)) {
                respond_json(callback, make_error_body(*error, "桥梁信息不完整或格式不正确。"), drogon::k400BadRequest); return;
            }
            db::BridgeAdministrationRepository repository(db_client);
            const auto outcome = repository.create_bridge(parsed);
            if (outcome.status == db::CreateBridgeStatus::Duplicate) {
                auto error = make_error_body("bridge_already_exists", "相同桥名、路线编号和桩号的桥梁已经存在。");
                error["existing_bridge"] = bridge_summary_json(*outcome.bridge);
                respond_json(callback, error, drogon::k409Conflict); return;
            }
            if (outcome.status != db::CreateBridgeStatus::Created) { respond_db_unavailable(callback); return; }
            Json::Value response; response["bridge"] = bridge_summary_json(*outcome.bridge);
            respond_json(callback, response, drogon::k201Created);
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        "/api/bridges/deletion-impact",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
            std::optional<db::AuthUser> user;
            if (!require_admin(db_client, request, callback, user)) return;
            const auto body = request->getJsonObject();
            std::vector<std::string> ids;
            if (!body || parse_bridge_selection(*body, ids)) {
                respond_json(callback, make_error_body("invalid_bridge_selection", "请选择 1 至 100 座有效桥梁。"), drogon::k400BadRequest); return;
            }
            db::BridgeDeletionRepository repository(db_client);
            std::vector<deletion::BridgeDeletionPlan> plans;
            deletion::BridgeDeletionCounts totals;
            for (const auto& id : ids) {
                auto plan = repository.preview(id);
                if (!plan) {
                    respond_json(callback, make_error_body("bridge_selection_changed", "桥梁列表已经变化，请刷新后重新选择。"), drogon::k409Conflict); return;
                }
                totals += plan->counts; plans.push_back(std::move(*plan));
            }
            std::sort(plans.begin(), plans.end(), [](const auto& a, const auto& b) { return a.bridge_system_number < b.bridge_system_number; });
            Json::Value response; response["bridges"] = Json::Value(Json::arrayValue);
            std::vector<std::string> numbers;
            for (const auto& plan : plans) { response["bridges"].append(plan.to_public_json()); numbers.push_back(plan.bridge_system_number); }
            response["totals"] = totals.to_json();
            response["confirmation_text"] = deletion::bridge_deletion_confirmation_text(numbers);
            respond_json(callback, response);
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        "/api/bridges",
        [db_client, cleanup_coordinator](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
            std::optional<db::AuthUser> user;
            if (!require_admin(db_client, request, callback, user)) return;
            const auto body = request->getJsonObject();
            DeleteBridgesRequest parsed;
            if (!body || parse_delete_bridges_request(*body, parsed)) {
                respond_json(callback, make_error_body("invalid_bridge_deletion_request", "请使用最新预览并填写删除原因和确认文字。"), drogon::k400BadRequest); return;
            }
            db::BridgeDeletionRepository repository(db_client);
            struct Work { DeleteBridgeItemRequest item; deletion::BridgeDeletionPlan plan; };
            std::vector<Work> work;
            for (const auto& item : parsed.items) {
                auto plan = repository.preview(item.bridge_id);
                if (!plan) {
                    respond_json(callback, make_error_body("bridge_selection_changed", "桥梁列表已经变化，请刷新后重新选择。"), drogon::k409Conflict); return;
                }
                work.push_back({item, std::move(*plan)});
            }
            std::sort(work.begin(), work.end(), [](const auto& a, const auto& b) { return a.plan.bridge_system_number < b.plan.bridge_system_number; });
            std::vector<std::string> numbers; for (const auto& item : work) numbers.push_back(item.plan.bridge_system_number);
            if (parsed.confirmation_text != deletion::bridge_deletion_confirmation_text(numbers)) {
                respond_json(callback, make_error_body("invalid_bridge_deletion_request", "确认文字不正确。"), drogon::k400BadRequest); return;
            }
            const auto batch_id = db_client->execSqlSync("select gen_random_uuid()::text as id")[0]["id"].as<std::string>();
            Json::Value response; response["batch_id"] = batch_id; response["results"] = Json::Value(Json::arrayValue);
            const deletion::DeletionActorSnapshot actor{user->id, user->username, user->display_name};
            for (const auto& item : work) {
                const auto outcome = repository.delete_bridge(item.item.bridge_id, item.item.impact_token, parsed.reason, actor, batch_id);
                Json::Value result; result["bridge_id"] = item.item.bridge_id; result["system_number"] = item.plan.bridge_system_number; result["bridge_name"] = item.plan.bridge_name;
                switch (outcome.status) {
                    case deletion::DeleteBridgeStatus::Deleted: {
                        result["status"] = "deleted"; result["audit_id"] = *outcome.deletion_audit_id;
                        int pending = item.plan.counts.archived_files_to_delete;
                        try {
                            cleanup_coordinator->process_bridge_audit(*outcome.deletion_audit_id);
                            pending = cleanup_coordinator->pending_bridge_items(*outcome.deletion_audit_id);
                        } catch (...) {}
                        result["file_cleanup_status"] = pending == 0 ? "completed" : "pending";
                        result["pending_file_count"] = pending;
                        result["total_file_count"] = item.plan.counts.archived_files_to_delete; break;
                    }
                    case deletion::DeleteBridgeStatus::Locked: result["status"] = "locked"; result["message"] = "该桥梁有导入记录正在编辑。"; break;
                    case deletion::DeleteBridgeStatus::ImpactChanged: result["status"] = "impact_changed"; result["message"] = "删除影响范围已经变化，请重新预览。"; break;
                    case deletion::DeleteBridgeStatus::NotFound: result["status"] = "not_found"; result["message"] = "桥梁已经不存在。"; break;
                    case deletion::DeleteBridgeStatus::Failed: result["status"] = "failed"; result["message"] = "该桥梁删除失败，数据库已回滚。"; break;
                }
                response["results"].append(std::move(result));
            }
            respond_json(callback, response);
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Delete});

    // 删除后前端轮询：每次调用先推进一批独占文件清理，再回报进度。既给确定性进度条，
    // 又不必等 5 分钟一次的后台定时器，也不依赖后端是否被频繁重启。
    register_options_handler("/api/bridge-deletion-audits/{audit_id}/cleanup/advance");
    drogon::app().registerHandler(
        "/api/bridge-deletion-audits/{audit_id}/cleanup/advance",
        [db_client, cleanup_coordinator](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                                         const std::string& audit_id) {
            try {
                std::optional<db::AuthUser> user;
                if (!require_admin(db_client, request, callback, user)) return;
                if (!is_valid_uuid(audit_id)) {
                    respond_json(callback, make_error_body("bridge_deletion_audit_not_found", "删除审计不存在。"),
                                 drogon::k404NotFound); return;
                }
                const auto exists = db_client->execSqlSync(
                    "select 1 from bridge_deletion_audits where id=$1::uuid", audit_id);
                if (exists.empty()) {
                    respond_json(callback, make_error_body("bridge_deletion_audit_not_found", "删除审计不存在。"),
                                 drogon::k404NotFound); return;
                }
                try { cleanup_coordinator->process_bridge_audit(audit_id); } catch (...) {}
                const auto counts = db_client->execSqlSync(
                    "select count(*)::int as total,"
                    "count(*) filter (where status='已完成')::int as completed,"
                    "count(*) filter (where status='失败待重试')::int as failed,"
                    "count(*) filter (where status<>'已完成')::int as pending "
                    "from bridge_archived_file_deletion_queue where bridge_deletion_audit_id=$1::uuid",
                    audit_id);
                Json::Value body;
                const int pending = counts[0]["pending"].as<int>();
                body["total"] = counts[0]["total"].as<int>();
                body["completed"] = counts[0]["completed"].as<int>();
                body["failed"] = counts[0]["failed"].as<int>();
                body["pending"] = pending;
                body["done"] = pending == 0;
                respond_json(callback, body);
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});
}

}  // namespace bridge_report::http
