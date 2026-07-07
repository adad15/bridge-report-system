#include "bridge_report/http/ReviewRoutes.hpp"

#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>

#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>
#include <json/json.h>

#include "bridge_report/db/ReviewRepository.hpp"
#include "bridge_report/http/Cors.hpp"
#include "bridge_report/review/ReviewStatistics.hpp"

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

void respond_import_record_not_found(const HttpCallback& callback) {
    respond_json(
        callback,
        make_error_body("import_record_not_found", "指定的导入记录不存在"),
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

// parsed_result_json 存储为 jsonb 文本；解析失败（理论上不应发生，防御式处理）时退化为空对象，
// 使 build_review_statistics 等下游逻辑仍能得到全 0 统计而不是崩溃。
Json::Value parse_parsed_result_json(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return Json::Value(Json::objectValue);
    }
    return root;
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
            } catch (const std::exception&) {
                // 兜底：处理器内不允许任何异常向外逃逸（与 main.cpp /health/db 的约定一致）。
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );
}

// 导入记录详情路由：GET /api/import-records/{import_record_id}/review。
// 与 register_bridge_scoped_route 类似，但作用域是导入记录而非桥梁，
// 且响应体需要联查桥梁/年度并叠加纯函数统计。
void register_import_record_review_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/import-records/{import_record_id}/review",
        [db_client](
            const drogon::HttpRequestPtr&,
            HttpCallback&& callback,
            const std::string& import_record_id
        ) {
            if (!is_valid_uuid(import_record_id)) {
                respond_import_record_not_found(callback);
                return;
            }

            try {
                db::ReviewRepository repository(db_client);
                const auto detail = repository.get_import_record_detail(import_record_id);
                if (!detail.has_value()) {
                    respond_import_record_not_found(callback);
                    return;
                }

                Json::Value import_record;
                import_record["id"] = detail->id;
                import_record["system_number"] = detail->system_number;
                import_record["import_name"] = detail->import_name;
                import_record["source_type"] = detail->source_type;
                import_record["import_status"] = detail->import_status;
                import_record["importer_name"] =
                    detail->importer_name.has_value() ? Json::Value(*detail->importer_name) : Json::Value(Json::nullValue);
                import_record["importer_version"] =
                    detail->importer_version.has_value() ? Json::Value(*detail->importer_version) : Json::Value(Json::nullValue);
                import_record["created_at"] = detail->created_at;
                import_record["updated_at"] = detail->updated_at;

                Json::Value bridge;
                bridge["id"] = detail->bridge_id;
                bridge["system_number"] = detail->bridge_system_number;
                bridge["bridge_name"] = detail->bridge_name;
                bridge["route_name"] =
                    detail->bridge_route_name.has_value() ? Json::Value(*detail->bridge_route_name) : Json::Value(Json::nullValue);

                Json::Value inspection_year(Json::nullValue);
                if (detail->inspection_year_id.has_value()) {
                    inspection_year = Json::Value(Json::objectValue);
                    inspection_year["id"] = *detail->inspection_year_id;
                    inspection_year["system_number"] =
                        detail->inspection_year_system_number.has_value() ? *detail->inspection_year_system_number : "";
                    inspection_year["inspection_year"] =
                        detail->inspection_year.has_value() ? *detail->inspection_year : 0;
                    inspection_year["status"] =
                        detail->inspection_year_status.has_value() ? *detail->inspection_year_status : "";
                    inspection_year["version_number"] =
                        detail->inspection_year_version_number.has_value() ? *detail->inspection_year_version_number : 0;
                    inspection_year["is_current"] =
                        detail->inspection_year_is_current.has_value() && *detail->inspection_year_is_current;
                }

                const auto parsed_result = parse_parsed_result_json(detail->parsed_result_json);
                const auto statistics = review::build_review_statistics(parsed_result);

                bool has_current_annual_facts = false;
                if (detail->inspection_year.has_value()) {
                    has_current_annual_facts =
                        repository.has_current_annual_facts(detail->bridge_id, *detail->inspection_year);
                } else if (
                    parsed_result.isMember("inspection") && parsed_result["inspection"].isObject()
                    && parsed_result["inspection"].isMember("inspection_year")
                    && parsed_result["inspection"]["inspection_year"].isInt()
                ) {
                    has_current_annual_facts = repository.has_current_annual_facts(
                        detail->bridge_id,
                        parsed_result["inspection"]["inspection_year"].asInt()
                    );
                }

                Json::Value body;
                body["import_record"] = import_record;
                body["bridge"] = bridge;
                body["inspection_year"] = inspection_year;
                body["parsed_result"] = parsed_result;
                body["statistics"] = statistics.to_json();
                body["has_current_annual_facts"] = has_current_annual_facts;

                respond_json(callback, body);
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

void register_review_routes(const drogon::orm::DbClientPtr& db_client) {
    register_options_handler("/api/bridges");
    register_options_handler("/api/bridges/{bridge_id}/inspection-years");
    register_options_handler("/api/bridges/{bridge_id}/import-records");
    register_options_handler("/api/import-records/{import_record_id}/review");

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
            } catch (const std::exception&) {
                // 兜底：处理器内不允许任何异常向外逃逸（与 main.cpp /health/db 的约定一致）。
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

    register_import_record_review_route(db_client);
}

}  // 命名空间 bridge_report::http
