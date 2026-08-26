#include "bridge_report/http/ComponentArchiveRoutes.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <utility>

#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>
#include <json/json.h>

#include "bridge_report/db/ComponentArchiveRepository.hpp"
#include "bridge_report/db/TriageQueryRepository.hpp"
#include "bridge_report/http/ReviewRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"
#include "bridge_report/review/ThreadSuggestions.hpp"

namespace bridge_report::http {

namespace {

void respond_bridge_not_found(const HttpCallback& callback) {
    respond_json(callback, make_error_body("bridge_not_found", "指定的桥梁不存在"), drogon::k404NotFound);
}

void respond_component_not_found(const HttpCallback& callback) {
    respond_json(callback, make_error_body("bridge_component_not_found", "指定的桥梁构件不存在"), drogon::k404NotFound);
}

bool bridge_exists(const drogon::orm::DbClientPtr& db_client, const std::string& bridge_id) {
    const auto result = db_client->execSqlSync("select 1 from bridges where id = $1::uuid", bridge_id);
    return !result.empty();
}

// 桥梁作用域只读路由骨架：uuid 校验 -> 桥梁存在 -> 组装响应；异常统一 503。
void register_bridge_scoped_route(
    const std::string& path,
    const drogon::orm::DbClientPtr& db_client,
    std::function<Json::Value(db::ComponentArchiveRepository&, const std::string&)> build_body
) {
    drogon::app().registerHandler(
        path,
        [db_client, build_body = std::move(build_body)](
            const drogon::HttpRequestPtr&, HttpCallback&& callback, const std::string& bridge_id
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
                db::ComponentArchiveRepository repository(db_client);
                respond_json(callback, build_body(repository, bridge_id));
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );
}

// 构件作用域只读路由骨架：uuid 校验 -> 构件存在 -> 以构件信息组装响应。
void register_component_scoped_route(
    const std::string& path,
    const drogon::orm::DbClientPtr& db_client,
    std::function<Json::Value(db::ComponentArchiveRepository&, const Json::Value&, const std::string&)> build_body
) {
    drogon::app().registerHandler(
        path,
        [db_client, build_body = std::move(build_body)](
            const drogon::HttpRequestPtr&, HttpCallback&& callback, const std::string& component_id
        ) {
            if (!is_valid_uuid(component_id)) {
                respond_component_not_found(callback);
                return;
            }
            try {
                db::ComponentArchiveRepository repository(db_client);
                const auto component = repository.get_component(component_id);
                if (!component.has_value()) {
                    respond_component_not_found(callback);
                    return;
                }
                respond_json(callback, build_body(repository, *component, component_id));
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );
}

// 线索建议：候选集限定为观测所属构件的线索，纯函数排序，只读、不持久化、绝不自动绑定。
void register_thread_suggestions_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/defect-observations/{observation_id}/thread-suggestions",
        [db_client](const drogon::HttpRequestPtr&, HttpCallback&& callback, const std::string& observation_id) {
            if (!is_valid_uuid(observation_id)) {
                respond_json(callback, make_error_body("defect_observation_not_found", "指定的病害观测不存在"),
                             drogon::k404NotFound);
                return;
            }
            try {
                db::ComponentArchiveRepository repository(db_client);
                const auto summary = repository.get_observation_summary(observation_id);
                if (!summary.has_value()) {
                    respond_json(callback, make_error_body("defect_observation_not_found", "指定的病害观测不存在"),
                                 drogon::k404NotFound);
                    return;
                }
                const auto threads =
                    repository.list_threads_for_component((*summary)["bridge_component_id"].asString());
                review::ThreadSuggestionInput input;
                input.defect_type = (*summary)["defect_type"].asString();
                input.defect_location =
                    (*summary)["defect_location"].isString() ? (*summary)["defect_location"].asString() : "";

                Json::Value body;
                body["observation"] = *summary;
                body["suggestions"] = review::suggest_threads(input, threads);
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

void register_observation_evidence_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/defect-observations/{observation_id}/evidence",
        [db_client](const drogon::HttpRequestPtr&, HttpCallback&& callback, const std::string& observation_id) {
            if (!is_valid_uuid(observation_id)) {
                respond_json(callback, make_error_body("defect_observation_not_found", "指定的病害观测不存在"),
                             drogon::k404NotFound);
                return;
            }
            try {
                db::ComponentArchiveRepository repository(db_client);
                const auto evidence = repository.get_observation_evidence(observation_id);
                if (!evidence.has_value()) {
                    respond_json(callback, make_error_body("defect_observation_not_found", "指定的病害观测不存在"),
                                 drogon::k404NotFound);
                    return;
                }
                respond_json(callback, *evidence);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );
}

// 正式病害照片内容：defect_photos.archived_file_id -> archived_files.storage_relative_path。
// 错误码与模块 05 的导入照片内容接口对齐；路径必须落在归档根之下。
void register_defect_photo_content_route(
    const drogon::orm::DbClientPtr& db_client,
    const std::filesystem::path& archive_root
) {
    drogon::app().registerHandler(
        "/api/defect-photos/{defect_photo_id}/content",
        [db_client, archive_root](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                                  const std::string& defect_photo_id) {
            if (!is_valid_uuid(defect_photo_id)) {
                respond_json(callback, make_error_body("defect_photo_not_found", "指定的病害照片不存在"),
                             drogon::k404NotFound);
                return;
            }
            try {
                db::ComponentArchiveRepository repository(db_client);
                const auto photo_exists = db_client->execSqlSync(
                    "select 1 from defect_photos where id = $1::uuid", defect_photo_id);
                if (photo_exists.empty()) {
                    respond_json(callback, make_error_body("defect_photo_not_found", "指定的病害照片不存在"),
                                 drogon::k404NotFound);
                    return;
                }
                const auto reference = repository.get_defect_photo_content_ref(defect_photo_id);
                if (!reference.has_value()) {
                    respond_json(callback, make_error_body("photo_archive_missing", "病害照片没有关联归档文件。"),
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

}  // namespace

void register_component_archive_routes(
    const drogon::orm::DbClientPtr& db_client,
    const std::filesystem::path& archive_root
) {
    register_options_handler("/api/bridges/{bridge_id}/components");
    register_options_handler("/api/bridge-components/{component_id}/defect-archive");
    register_options_handler("/api/bridge-components/{component_id}/defect-archive/revisions");
    register_options_handler("/api/bridges/{bridge_id}/unbound-defect-observations");
    register_options_handler("/api/bridges/{bridge_id}/thread-triage");
    register_options_handler("/api/defect-observations/{observation_id}/evidence");
    register_options_handler("/api/defect-observations/{observation_id}/thread-suggestions");
    register_options_handler("/api/defect-photos/{defect_photo_id}/content");

    register_bridge_scoped_route(
        "/api/bridges/{bridge_id}/components", db_client,
        [](db::ComponentArchiveRepository& repository, const std::string& bridge_id) {
            return repository.list_components(bridge_id);
        }
    );

    register_bridge_scoped_route(
        "/api/bridges/{bridge_id}/unbound-defect-observations", db_client,
        [](db::ComponentArchiveRepository& repository, const std::string& bridge_id) {
            return repository.list_unbound_observations(bridge_id);
        }
    );

    // 线索整理工作台摘要：一次请求给出全部批次与异常簇，前端不再逐观测发候选请求。
    drogon::app().registerHandler(
        "/api/bridges/{bridge_id}/thread-triage",
        [db_client](
            const drogon::HttpRequestPtr&, HttpCallback&& callback, const std::string& bridge_id) {
            if (!is_valid_uuid(bridge_id)) {
                respond_bridge_not_found(callback);
                return;
            }
            try {
                if (!bridge_exists(db_client, bridge_id)) {
                    respond_bridge_not_found(callback);
                    return;
                }
                respond_json(callback, db::TriageQueryRepository(db_client).summary(bridge_id));
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );

    register_component_scoped_route(
        "/api/bridge-components/{component_id}/defect-archive", db_client,
        [](db::ComponentArchiveRepository& repository, const Json::Value&, const std::string& component_id) {
            return repository.get_defect_archive(component_id);
        }
    );

    register_component_scoped_route(
        "/api/bridge-components/{component_id}/defect-archive/revisions", db_client,
        [](db::ComponentArchiveRepository& repository, const Json::Value& component, const std::string& component_id) {
            return repository.get_revisions(component_id, component["bridge_id"].asString());
        }
    );

    register_observation_evidence_route(db_client);
    register_thread_suggestions_route(db_client);
    register_defect_photo_content_route(db_client, archive_root);
}

}  // namespace bridge_report::http
