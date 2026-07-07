#include "bridge_report/http/ReviewRoutes.hpp"

#include <functional>
#include <regex>
#include <string>
#include <utility>

#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>

#include "bridge_report/db/ReviewRepository.hpp"
#include "bridge_report/http/Cors.hpp"

namespace bridge_report::http {

namespace {

using HttpCallback = std::function<void(const drogon::HttpResponsePtr&)>;

// 先用正则校验路径参数，避免把非法 uuid 引发的 SQL 异常与数据库故障混为一谈。
bool is_valid_uuid(const std::string& value) {
    static const std::regex uuid_pattern(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
    );
    return std::regex_match(value, uuid_pattern);
}

Json::Value make_error_body(const std::string& code, const std::string& message) {
    Json::Value body;
    body["code"] = code;
    body["message"] = message;
    return body;
}

void respond_json(
    const HttpCallback& callback,
    const Json::Value& body,
    drogon::HttpStatusCode status = drogon::k200OK
) {
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(status);
    apply_local_dev_cors_headers(response);
    callback(response);
}

void respond_bridge_not_found(const HttpCallback& callback) {
    respond_json(
        callback,
        make_error_body("bridge_not_found", "指定的桥梁不存在"),
        drogon::k404NotFound
    );
}

void respond_db_unavailable(const HttpCallback& callback) {
    respond_json(
        callback,
        make_error_body("db_unavailable", "数据库暂不可用，请稍后重试。"),
        drogon::k503ServiceUnavailable
    );
}

// 注意：execSqlSync 会阻塞当前 IO 线程；本地单用户 v1 场景可接受（与 /health/db 的取舍一致）。
bool bridge_exists(const drogon::orm::DbClientPtr& db_client, const std::string& bridge_id) {
    const auto result = db_client->execSqlSync("select 1 from bridges where id = $1::uuid", bridge_id);
    return !result.empty();
}

void register_options_handler(const std::string& path) {
    drogon::app().registerHandler(
        path,
        [](const drogon::HttpRequestPtr&, HttpCallback&& callback) {
            auto response = drogon::HttpResponse::newHttpResponse();
            apply_local_dev_cors_headers(response);
            callback(response);
        },
        {drogon::Options, "drogon::HttpOptionsMiddleware"}
    );
}

// 桥梁子资源路由的公共骨架：先校验 uuid，再确认桥梁存在，最后交给 build_body 组装响应体。
// 走到查询这一步时 uuid 一定合法，SQL 异常只可能是数据库故障，统一回 503。
void register_bridge_scoped_route(
    const std::string& path,
    const drogon::orm::DbClientPtr& db_client,
    std::function<Json::Value(db::ReviewRepository&, const std::string&)> build_body
) {
    drogon::app().registerHandler(
        path,
        [db_client, build_body = std::move(build_body)](
            const drogon::HttpRequestPtr&,
            HttpCallback&& callback,
            const std::string& bridge_id
        ) {
            if (!is_valid_uuid(bridge_id)) {
                respond_bridge_not_found(callback);
                return;
            }

            try {
                if (!bridge_exists(db_client, bridge_id)) {
                    respond_bridge_not_found(callback);
                    return;
                }

                db::ReviewRepository repository(db_client);
                respond_json(callback, build_body(repository, bridge_id));
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );
}

}  // 匿名命名空间

void register_review_routes(const drogon::orm::DbClientPtr& db_client) {
    register_options_handler("/api/bridges");
    register_options_handler("/api/bridges/{bridge_id}/inspection-years");
    register_options_handler("/api/bridges/{bridge_id}/import-records");

    drogon::app().registerHandler(
        "/api/bridges",
        [db_client](const drogon::HttpRequestPtr&, HttpCallback&& callback) {
            try {
                db::ReviewRepository repository(db_client);
                const auto bridges = repository.list_bridges();

                Json::Value body;
                body["bridges"] = Json::Value(Json::arrayValue);
                for (const auto& bridge : bridges) {
                    body["bridges"].append(bridge.to_json());
                }
                respond_json(callback, body);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );

    register_bridge_scoped_route(
        "/api/bridges/{bridge_id}/inspection-years",
        db_client,
        [](db::ReviewRepository& repository, const std::string& bridge_id) {
            Json::Value body;
            body["inspection_years"] = Json::Value(Json::arrayValue);
            for (const auto& year : repository.list_inspection_years(bridge_id)) {
                body["inspection_years"].append(year.to_json());
            }
            return body;
        }
    );

    register_bridge_scoped_route(
        "/api/bridges/{bridge_id}/import-records",
        db_client,
        [](db::ReviewRepository& repository, const std::string& bridge_id) {
            Json::Value body;
            body["import_records"] = Json::Value(Json::arrayValue);
            for (const auto& record : repository.list_import_records(bridge_id)) {
                body["import_records"].append(record.to_json());
            }
            return body;
        }
    );
}

}  // 命名空间 bridge_report::http
