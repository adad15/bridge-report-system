#include "bridge_report/http/ReviewRoutes.hpp"

#include <functional>
#include <sstream>
#include <string>
#include <utility>

#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>
#include <json/json.h>

#include "bridge_report/db/ReviewRepository.hpp"
#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/http/RouteHelpers.hpp"
#include "bridge_report/review/ContractCompatibility.hpp"
#include "bridge_report/review/DraftValidation.hpp"
#include "bridge_report/review/ReviewModels.hpp"
#include "bridge_report/review/ReviewStatistics.hpp"

namespace bridge_report::http {

std::optional<std::filesystem::path> resolve_photo_content_path(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& storage_relative_path
) {
    try {
        return archive::resolve_path_under_root(archive_root, storage_relative_path);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

namespace {

void respond_bridge_not_found(const HttpCallback& callback) {
    respond_json(
        callback,
        make_error_body("bridge_not_found", "指定的桥梁不存在"),
        drogon::k404NotFound
    );
}

// respond_import_record_not_found / parse_parsed_result_json / register_options_handler
// 现由 RouteHelpers.hpp 提供（与 ImportConfirmRoutes.cpp 共用）。

// 注意：execSqlSync 会阻塞当前 IO 线程；本地单用户 v1 场景可接受（与 /health/db 的取舍一致）。
bool bridge_exists(const drogon::orm::DbClientPtr& db_client, const std::string& bridge_id) {
    const auto result = db_client->execSqlSync("select 1 from bridges where id = $1::uuid", bridge_id);
    return !result.empty();
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

                auto parsed_result = parse_parsed_result_json(detail->parsed_result_json);
                const auto normalized = review::normalize_review_contract(
                    std::move(parsed_result),
                    detail->import_status
                );
                const auto statistics = review::build_review_statistics(normalized.data);

                // 年度事实是否已存在的判定归属数据库决策，留在路由层；
                // 有效年度的解析（挂载年度优先，否则退化到解析结果里的年度）由纯函数
                // resolve_effective_inspection_year 承担，与 preflight-confirm 路由共用；
                // JSON 形状拼装则委托给纯函数 build_review_response（见 ReviewModels.cpp）。
                const auto effective_year = review::resolve_effective_inspection_year(*detail, normalized.data);
                const bool has_current_annual_facts = effective_year.has_value()
                    && repository.has_current_annual_facts(detail->bridge_id, *effective_year);

                const auto body =
                    review::build_review_response(
                        *detail,
                        normalized.data,
                        statistics,
                        has_current_annual_facts,
                        review::contract_compatibility_name(normalized.compatibility)
                    );

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

const Json::Value* find_photo_candidate(const Json::Value& data, const std::string& candidate_id) {
    if (!data["photos"].isArray()) return nullptr;
    for (const auto& photo : data["photos"]) {
        if (photo["candidate_id"].isString() && photo["candidate_id"].asString() == candidate_id) return &photo;
    }
    return nullptr;
}

void register_photo_content_route(
    const drogon::orm::DbClientPtr& db_client,
    const std::filesystem::path& archive_root
) {
    drogon::app().registerHandler(
        "/api/import-records/{import_record_id}/photos/{photo_candidate_id}/content",
        [db_client, archive_root](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                                  const std::string& import_record_id, const std::string& photo_candidate_id) {
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
                const auto data = parse_parsed_result_json(detail->parsed_result_json);
                const auto* photo = find_photo_candidate(data, photo_candidate_id);
                if (photo == nullptr) {
                    respond_json(callback, make_error_body("photo_candidate_not_found", "指定的照片候选不存在。"),
                                 drogon::k404NotFound);
                    return;
                }
                const auto& path_value = (*photo)["extracted_file"]["archive_relative_path"];
                if (!path_value.isString() || path_value.asString().empty()) {
                    respond_json(callback, make_error_body("photo_archive_missing", "照片候选尚无归档文件。"),
                                 drogon::k409Conflict);
                    return;
                }
                const auto reference = repository.get_photo_content_ref(import_record_id, photo_candidate_id);
                if (!reference.has_value()) {
                    respond_json(callback, make_error_body("photo_archive_missing", "照片归档未关联到当前导入记录。"),
                                 drogon::k409Conflict);
                    return;
                }
                const auto resolved = resolve_photo_content_path(archive_root, reference->storage_relative_path);
                if (!resolved.has_value()) {
                    respond_json(callback, make_error_body("unsafe_archive_path", "照片归档路径不安全。"),
                                 drogon::k400BadRequest);
                    return;
                }
                if (!std::filesystem::is_regular_file(*resolved)) {
                    respond_json(callback, make_error_body("photo_archive_missing", "照片归档文件不存在。"),
                                 drogon::k409Conflict);
                    return;
                }
                auto response = drogon::HttpResponse::newFileResponse(
                    resolved->string(), "", drogon::CT_CUSTOM, reference->content_type, request);
                apply_local_dev_cors_headers(response);
                callback(response);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::filesystem::filesystem_error&) {
                respond_json(callback, make_error_body("photo_archive_missing", "照片归档文件无法读取。"),
                             drogon::k409Conflict);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );
}

}  // 匿名命名空间

void register_review_routes(const drogon::orm::DbClientPtr& db_client, const std::filesystem::path& archive_root) {
    register_options_handler("/api/bridges");
    register_options_handler("/api/bridges/{bridge_id}/inspection-years");
    register_options_handler("/api/bridges/{bridge_id}/import-records");
    register_options_handler("/api/import-records/{import_record_id}/review");
    register_options_handler("/api/import-records/{import_record_id}/review-draft");
    register_options_handler("/api/import-records/{import_record_id}/cancel");
    register_options_handler("/api/import-records/{import_record_id}/photos/{photo_candidate_id}/content");

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
    register_photo_content_route(db_client, archive_root);
}

}  // 命名空间 bridge_report::http
