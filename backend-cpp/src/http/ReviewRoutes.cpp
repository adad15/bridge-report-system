#include "bridge_report/http/ReviewRoutes.hpp"

#include <functional>
#include <string>

#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>

#include "bridge_report/db/ReviewRepository.hpp"
#include "bridge_report/http/Cors.hpp"

namespace bridge_report::http {

namespace {

Json::Value make_error_body(const std::string& code, const std::string& message) {
    Json::Value body;
    body["code"] = code;
    body["message"] = message;
    return body;
}

drogon::HttpResponsePtr make_bridge_not_found_response() {
    auto response = drogon::HttpResponse::newHttpJsonResponse(
        make_error_body("bridge_not_found", "指定的桥梁不存在")
    );
    response->setStatusCode(drogon::k404NotFound);
    apply_local_dev_cors_headers(response);
    return response;
}

// 注意：execSqlSync 会阻塞当前 IO 线程；本地单用户 v1 场景可接受（与 /health/db 的取舍一致）。
bool bridge_exists(const drogon::orm::DbClientPtr& db_client, const std::string& bridge_id) {
    const auto result = db_client->execSqlSync("select 1 from bridges where id = $1::uuid", bridge_id);
    return !result.empty();
}

void register_options_handler(const std::string& path) {
    drogon::app().registerHandler(
        path,
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto response = drogon::HttpResponse::newHttpResponse();
            apply_local_dev_cors_headers(response);
            callback(response);
        },
        {drogon::Options, "drogon::HttpOptionsMiddleware"}
    );
}

}  // 匿名命名空间

void register_review_routes(const drogon::orm::DbClientPtr& db_client) {
    register_options_handler("/api/bridges");
    register_options_handler("/api/bridges/{bridge_id}/inspection-years");
    register_options_handler("/api/bridges/{bridge_id}/import-records");

    drogon::app().registerHandler(
        "/api/bridges",
        [db_client](
            const drogon::HttpRequestPtr&,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback
        ) {
            db::ReviewRepository repository(db_client);
            const auto bridges = repository.list_bridges();

            Json::Value body;
            body["bridges"] = Json::Value(Json::arrayValue);
            for (const auto& bridge : bridges) {
                body["bridges"].append(bridge.to_json());
            }

            auto response = drogon::HttpResponse::newHttpJsonResponse(body);
            apply_local_dev_cors_headers(response);
            callback(response);
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/api/bridges/{bridge_id}/inspection-years",
        [db_client](
            const drogon::HttpRequestPtr&,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string& bridge_id
        ) {
            try {
                if (!bridge_exists(db_client, bridge_id)) {
                    callback(make_bridge_not_found_response());
                    return;
                }

                db::ReviewRepository repository(db_client);
                const auto years = repository.list_inspection_years(bridge_id);

                Json::Value body;
                body["inspection_years"] = Json::Value(Json::arrayValue);
                for (const auto& year : years) {
                    body["inspection_years"].append(year.to_json());
                }

                auto response = drogon::HttpResponse::newHttpJsonResponse(body);
                apply_local_dev_cors_headers(response);
                callback(response);
            } catch (const drogon::orm::DrogonDbException&) {
                // 非法 uuid 字符串等会导致 SQL 执行抛出异常，按未找到处理。
                callback(make_bridge_not_found_response());
            }
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/api/bridges/{bridge_id}/import-records",
        [db_client](
            const drogon::HttpRequestPtr&,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string& bridge_id
        ) {
            try {
                if (!bridge_exists(db_client, bridge_id)) {
                    callback(make_bridge_not_found_response());
                    return;
                }

                db::ReviewRepository repository(db_client);
                const auto records = repository.list_import_records(bridge_id);

                Json::Value body;
                body["import_records"] = Json::Value(Json::arrayValue);
                for (const auto& record : records) {
                    body["import_records"].append(record.to_json());
                }

                auto response = drogon::HttpResponse::newHttpJsonResponse(body);
                apply_local_dev_cors_headers(response);
                callback(response);
            } catch (const drogon::orm::DrogonDbException&) {
                callback(make_bridge_not_found_response());
            }
        },
        {drogon::Get}
    );
}

}  // 命名空间 bridge_report::http
