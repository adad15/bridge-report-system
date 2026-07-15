#include "bridge_report/http/AuthRoutes.hpp"

#include <string>

#include <drogon/orm/Exception.h>
#include <json/json.h>

#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {
namespace {

// 登录响应与 /api/auth/me 共用的用户视图；绝不含 id 之外的内部字段与口令信息。
Json::Value user_to_json(const db::AuthUser& user) {
    Json::Value body;
    body["username"] = user.username;
    body["display_name"] = user.display_name;
    body["role"] = user.role;
    return body;
}

std::string extract_bearer_token(const drogon::HttpRequestPtr& request) {
    const auto& header = request->getHeader("authorization");
    const std::string prefix = "Bearer ";
    if (header.size() <= prefix.size() || header.compare(0, prefix.size(), prefix) != 0) {
        return std::string();
    }
    return header.substr(prefix.size());
}

void register_login_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/auth/login",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            const auto body_json = request->getJsonObject();
            if (body_json == nullptr || !(*body_json)["username"].isString()
                || !(*body_json)["password"].isString()) {
                respond_json(
                    callback,
                    make_error_body("invalid_json_body", "请求体必须包含 username 与 password 字符串。"),
                    drogon::k400BadRequest
                );
                return;
            }

            try {
                db::AuthRepository repository(db_client);
                const auto outcome = repository.login(
                    (*body_json)["username"].asString(),
                    (*body_json)["password"].asString()
                );
                if (!outcome.has_value()) {
                    // 用户不存在 / 停用 / 口令不符统一回一个码，避免枚举用户名。
                    respond_json(
                        callback,
                        make_error_body("invalid_credentials", "用户名或密码不正确。"),
                        drogon::k401Unauthorized
                    );
                    return;
                }

                Json::Value response_body;
                response_body["token"] = outcome->second;
                response_body["user"] = user_to_json(outcome->first);
                respond_json(callback, response_body);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );
}

void register_logout_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/auth/logout",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                db::AuthRepository repository(db_client);
                repository.delete_session_by_token(extract_bearer_token(request));

                Json::Value response_body;
                response_body["logged_out"] = true;
                respond_json(callback, response_body);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );
}

void register_me_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/auth/me",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                const auto user = authenticate_request(db_client, request);
                if (!user.has_value()) {
                    respond_unauthorized(callback);
                    return;
                }

                Json::Value response_body;
                response_body["user"] = user_to_json(*user);
                respond_json(callback, response_body);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );
}

}  // 匿名命名空间

std::optional<db::AuthUser> authenticate_request(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request
) {
    const auto token = extract_bearer_token(request);
    if (token.empty()) {
        return std::nullopt;
    }
    db::AuthRepository repository(db_client);
    return repository.find_user_by_token(token);
}

void register_auth_routes(const drogon::orm::DbClientPtr& db_client) {
    register_options_handler("/api/auth/login");
    register_options_handler("/api/auth/logout");
    register_options_handler("/api/auth/me");

    register_login_route(db_client);
    register_logout_route(db_client);
    register_me_route(db_client);
}

}  // 命名空间 bridge_report::http
