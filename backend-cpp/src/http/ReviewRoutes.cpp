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
#include "bridge_report/review/DraftValidation.hpp"
#include "bridge_report/review/ReviewModels.hpp"
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

                const auto parsed_result = parse_parsed_result_json(detail->parsed_result_json);
                const auto statistics = review::build_review_statistics(parsed_result);

                // 年度事实是否已存在的判定归属数据库决策，留在路由层；
                // JSON 形状拼装则委托给纯函数 build_review_response（见 ReviewModels.cpp）。
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

                const auto body =
                    review::build_review_response(*detail, parsed_result, statistics, has_current_annual_facts);

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

void respond_invalid_json_body(const HttpCallback& callback) {
    respond_json(
        callback,
        make_error_body("invalid_json_body", "请求体不是合法的 JSON。"),
        drogon::k400BadRequest
    );
}

Json::Value make_draft_validation_error_body(const review::DraftValidationResult& validation) {
    auto body = make_error_body(validation.code, validation.message);
    if (!validation.issues.empty()) {
        Json::Value issues(Json::arrayValue);
        for (const auto& issue : validation.issues) {
            Json::Value issue_json;
            issue_json["path"] = issue.path;
            issue_json["message"] = issue.message;
            issues.append(issue_json);
        }
        body["issues"] = issues;
    }
    return body;
}

// 校验失败到 HTTP 状态码的映射：not_editable 是状态冲突 -> 409，其余两类是请求体本身的问题 -> 400。
drogon::HttpStatusCode draft_validation_status_code(const std::string& code) {
    if (code == "import_record_not_editable") {
        return drogon::k409Conflict;
    }
    return drogon::k400BadRequest;
}

// PUT /api/import-records/{import_record_id}/review-draft：保存校对草稿。
// 校验顺序：uuid 合法 -> 请求体是合法 JSON -> 记录存在 -> validate_review_draft -> 保存。
void register_save_review_draft_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/import-records/{import_record_id}/review-draft",
        [db_client](
            const drogon::HttpRequestPtr& request,
            HttpCallback&& callback,
            const std::string& import_record_id
        ) {
            if (!is_valid_uuid(import_record_id)) {
                respond_import_record_not_found(callback);
                return;
            }

            const auto body_json = request->getJsonObject();
            if (body_json == nullptr) {
                respond_invalid_json_body(callback);
                return;
            }

            try {
                db::ReviewRepository repository(db_client);
                const auto detail = repository.get_import_record_detail(import_record_id);
                if (!detail.has_value()) {
                    respond_import_record_not_found(callback);
                    return;
                }

                const auto validation =
                    review::validate_review_draft(*body_json, detail->system_number, detail->import_status);
                if (!validation.ok) {
                    respond_json(
                        callback,
                        make_draft_validation_error_body(validation),
                        draft_validation_status_code(validation.code)
                    );
                    return;
                }

                // jsonb 列不保留输入格式，紧凑序列化即可，避免 toStyledString 的缩进开销。
                Json::StreamWriterBuilder writer_builder;
                writer_builder["indentation"] = "";
                const bool saved =
                    repository.save_review_draft(import_record_id, Json::writeString(writer_builder, *body_json));
                if (!saved) {
                    // UPDATE 带状态谓词未命中：记录状态在加载后被并发改变（已取消/已确认），拒绝写入。
                    respond_json(
                        callback,
                        make_error_body("import_record_not_editable", "导入记录状态已变化，无法保存草稿。"),
                        drogon::k409Conflict
                    );
                    return;
                }

                Json::Value response_body;
                response_body["saved"] = true;
                response_body["import_status"] = detail->import_status;
                respond_json(callback, response_body);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Put}
    );
}

// POST /api/import-records/{import_record_id}/cancel：取消导入。
void register_cancel_import_record_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/import-records/{import_record_id}/cancel",
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

                const bool cancelled = repository.cancel_import_record(import_record_id);
                if (!cancelled) {
                    respond_json(
                        callback,
                        make_error_body("import_record_not_editable", "导入记录当前状态不可取消。"),
                        drogon::k409Conflict
                    );
                    return;
                }

                Json::Value response_body;
                response_body["cancelled"] = true;
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

}  // 匿名命名空间

void register_review_routes(const drogon::orm::DbClientPtr& db_client) {
    register_options_handler("/api/bridges");
    register_options_handler("/api/bridges/{bridge_id}/inspection-years");
    register_options_handler("/api/bridges/{bridge_id}/import-records");
    register_options_handler("/api/import-records/{import_record_id}/review");
    register_options_handler("/api/import-records/{import_record_id}/review-draft");
    register_options_handler("/api/import-records/{import_record_id}/cancel");

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
    register_save_review_draft_route(db_client);
    register_cancel_import_record_route(db_client);
}

}  // 命名空间 bridge_report::http
