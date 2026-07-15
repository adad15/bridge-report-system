#include "bridge_report/http/EditLockRoutes.hpp"

#include <algorithm>
#include <cctype>

#include <drogon/orm/Exception.h>

#include "bridge_report/http/AuthRoutes.hpp"

namespace bridge_report::http {
namespace {

std::string trim_copy(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

void respond_lock_check_failure(const HttpCallback& callback, const db::EditLockCheckResult& result) {
    std::string code;
    std::string message;
    switch (result.status) {
        case db::EditLockCheckStatus::required:
            code = "edit_lock_required";
            message = "当前页面未持有该导入记录的编辑锁。";
            break;
        case db::EditLockCheckStatus::invalid:
            code = "edit_lock_invalid";
            message = "编辑锁不属于当前用户、会话或页面。";
            break;
        case db::EditLockCheckStatus::expired:
            code = "edit_lock_expired";
            message = "编辑锁已过期，请刷新页面后重新取得编辑权。";
            break;
        case db::EditLockCheckStatus::force_released:
            code = "edit_lock_force_released";
            message = "编辑权已被管理员收回，请刷新页面。";
            break;
        case db::EditLockCheckStatus::active:
            return;
    }
    auto body = make_error_body(code, message);
    if (result.lock.has_value()) {
        body["lock"] = edit_lock_info_to_json(*result.lock);
    }
    respond_json(callback, body, drogon::k409Conflict);
}

void register_acquire_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/import-records/{import_record_id}/edit-lock",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback, const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                const auto user = authenticate_request(db_client, request);
                if (!user.has_value()) {
                    respond_unauthorized(callback);
                    return;
                }
                db::EditLockRepository repository(db_client);
                const auto outcome = repository.acquire(id, *user);
                if (!outcome.import_record_found) {
                    respond_import_record_not_found(callback);
                    return;
                }
                if (!outcome.acquired) {
                    auto body = make_error_body("import_record_locked", "该导入记录正在被其他页面编辑。");
                    if (outcome.lock.has_value()) {
                        body["lock"] = edit_lock_info_to_json(*outcome.lock, user->id);
                    }
                    respond_json(callback, body, drogon::k409Conflict);
                    return;
                }
                Json::Value body;
                body["acquired"] = true;
                body["lock_token"] = outcome.lock_token;
                body["heartbeat_interval_seconds"] = 30;
                body["lock"] = edit_lock_info_to_json(*outcome.lock, user->id);
                respond_json(callback, body);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );
}

void register_heartbeat_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/import-records/{import_record_id}/edit-lock/heartbeat",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback, const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                const auto user = authenticate_request(db_client, request);
                if (!user.has_value()) {
                    respond_unauthorized(callback);
                    return;
                }
                db::EditLockRepository repository(db_client);
                const auto result = repository.heartbeat(id, *user, edit_lock_token_from_request(request));
                if (!result.active()) {
                    respond_lock_check_failure(callback, result);
                    return;
                }
                Json::Value body;
                body["renewed"] = true;
                body["lock"] = edit_lock_info_to_json(*result.lock, user->id);
                respond_json(callback, body);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );
}

void register_release_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/import-records/{import_record_id}/edit-lock",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback, const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                const auto user = authenticate_request(db_client, request);
                if (!user.has_value()) {
                    respond_unauthorized(callback);
                    return;
                }
                db::EditLockRepository repository(db_client);
                const bool released = repository.release(id, *user, edit_lock_token_from_request(request));
                Json::Value body;
                body["released"] = released;
                respond_json(callback, body);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Delete}
    );
}

void register_force_release_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/import-records/{import_record_id}/edit-lock/force-release",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback, const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_import_record_not_found(callback);
                return;
            }
            const auto body = request->getJsonObject();
            const auto reason = body != nullptr && (*body)["reason"].isString()
                ? trim_copy((*body)["reason"].asString()) : std::string();
            if (reason.empty()) {
                respond_json(callback, make_error_body("force_release_reason_required", "强制解锁必须填写原因。"),
                             drogon::k400BadRequest);
                return;
            }
            try {
                const auto user = authenticate_request(db_client, request);
                if (!user.has_value()) {
                    respond_unauthorized(callback);
                    return;
                }
                if (!user->is_admin()) {
                    respond_forbidden(callback);
                    return;
                }
                db::EditLockRepository repository(db_client);
                if (!repository.force_release(id, *user, reason)) {
                    respond_json(callback, make_error_body("edit_lock_not_found", "该记录当前没有活动编辑锁。"),
                                 drogon::k409Conflict);
                    return;
                }
                Json::Value response;
                response["released"] = true;
                respond_json(callback, response);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );
}

}  // namespace

std::string edit_lock_token_from_request(const drogon::HttpRequestPtr& request) {
    return request->getHeader("x-edit-lock-token");
}

Json::Value edit_lock_info_to_json(const db::EditLockInfo& lock, const std::string& current_user_id) {
    Json::Value body;
    body["owner_username"] = lock.username;
    body["owner_display_name"] = lock.display_name;
    body["owned_by_current_user"] = !current_user_id.empty() && current_user_id == lock.user_id;
    body["acquired_at"] = lock.acquired_at;
    body["expires_at"] = lock.expires_at;
    return body;
}

bool require_active_edit_lock(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const std::string& import_record_id,
    const db::AuthUser& user,
    const HttpCallback& callback
) {
    db::EditLockRepository repository(db_client);
    const auto result = repository.check(import_record_id, user, edit_lock_token_from_request(request));
    if (result.active()) {
        return true;
    }
    respond_lock_check_failure(callback, result);
    return false;
}

void register_edit_lock_routes(const drogon::orm::DbClientPtr& db_client) {
    register_options_handler("/api/import-records/{import_record_id}/edit-lock");
    register_options_handler("/api/import-records/{import_record_id}/edit-lock/heartbeat");
    register_options_handler("/api/import-records/{import_record_id}/edit-lock/force-release");
    register_acquire_route(db_client);
    register_heartbeat_route(db_client);
    register_release_route(db_client);
    register_force_release_route(db_client);
}

}  // namespace bridge_report::http
