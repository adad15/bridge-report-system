#include "bridge_report/http/WorkspaceRoutes.hpp"

#include <string>

#include <drogon/drogon.h>
#include <drogon/MultiPart.h>

#include "bridge_report/archive/WordInputArchive.hpp"
#include "bridge_report/db/WorkspaceRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {
namespace {

void respond_workspace_not_found(const HttpCallback& callback, const WorkspaceResource resource) {
    respond_json(callback, workspace_not_found_body(resource), drogon::k404NotFound);
}

}  // namespace

Json::Value workspace_not_found_body(const WorkspaceResource resource) {
    if (resource == WorkspaceResource::Bridge) {
        return make_error_body("bridge_not_found", "桥梁不存在。");
    }
    return make_error_body("inspection_year_not_found", "年度检测不存在。");
}

Json::Value inspection_year_already_exists_body(
    const int inspection_year,
    const std::string& existing_inspection_year_id
) {
    auto body = make_error_body(
        "inspection_year_already_exists",
        "该桥梁已经存在 " + std::to_string(inspection_year) + " 年度检测。"
    );
    body["existing_inspection_year_id"] = existing_inspection_year_id;
    return body;
}

bool is_supported_word_source_type(const std::string& source_type) {
    return source_type == "软件导出Word" || source_type == "正式Word";
}

void register_workspace_routes(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config
) {
    const std::string bridge_path = "/api/bridges/{bridge_id}/overview";
    const std::string inspection_path = "/api/inspection-years/{inspection_year_id}/workspace";
    const std::string word_upload_path = "/api/inspection-years/{inspection_year_id}/import-records/word";
    register_options_handler(bridge_path);
    register_options_handler(inspection_path);
    register_options_handler(word_upload_path);

    drogon::app().registerHandler(
        bridge_path,
        [db_client](const drogon::HttpRequestPtr&, HttpCallback&& callback, const std::string& bridge_id) {
            if (!is_valid_uuid(bridge_id)) {
                respond_workspace_not_found(callback, WorkspaceResource::Bridge);
                return;
            }
            try {
                db::WorkspaceRepository repository(db_client);
                const auto overview = repository.get_bridge_overview(bridge_id);
                if (!overview.has_value()) {
                    respond_workspace_not_found(callback, WorkspaceResource::Bridge);
                    return;
                }
                respond_json(callback, overview->to_json());
            } catch (const drogon::orm::DrogonDbException& error) {
                LOG_ERROR << "Word upload database failure: " << error.base().what();
                respond_db_unavailable(callback);
            } catch (const std::exception& error) {
                LOG_ERROR << "Word upload request handling failure: " << error.what();
                respond_json(callback,
                             make_error_body("word_upload_failed", "Word 上传处理失败，请稍后重试。"),
                             drogon::k500InternalServerError);
            }
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        inspection_path,
        [db_client](const drogon::HttpRequestPtr&, HttpCallback&& callback,
                    const std::string& inspection_year_id) {
            if (!is_valid_uuid(inspection_year_id)) {
                respond_workspace_not_found(callback, WorkspaceResource::InspectionYear);
                return;
            }
            try {
                db::WorkspaceRepository repository(db_client);
                const auto workspace = repository.get_inspection_workspace(inspection_year_id);
                if (!workspace.has_value()) {
                    respond_workspace_not_found(callback, WorkspaceResource::InspectionYear);
                    return;
                }
                respond_json(callback, workspace->to_json());
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );

    // 该路径的 GET 与 OPTIONS 已由年度列表路由注册，这里只追加创建动作。
    drogon::app().registerHandler(
        "/api/bridges/{bridge_id}/inspection-years",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& bridge_id) {
            if (!is_valid_uuid(bridge_id)) {
                respond_workspace_not_found(callback, WorkspaceResource::Bridge);
                return;
            }
            const auto request_body = request->getJsonObject();
            if (request_body == nullptr) {
                respond_json(callback, make_error_body("invalid_json_body", "请求体不是合法的 JSON。"),
                             drogon::k400BadRequest);
                return;
            }
            if (!request_body->isMember("inspection_year")
                || !(*request_body)["inspection_year"].isInt()) {
                respond_json(callback,
                             make_error_body("invalid_inspection_year", "检测年度必须是 1900 至 2200 的整数。"),
                             drogon::k400BadRequest);
                return;
            }
            const int inspection_year = (*request_body)["inspection_year"].asInt();
            if (inspection_year < 1900 || inspection_year > 2200) {
                respond_json(callback,
                             make_error_body("invalid_inspection_year", "检测年度必须是 1900 至 2200 的整数。"),
                             drogon::k400BadRequest);
                return;
            }

            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback);
                    return;
                }
                db::WorkspaceRepository repository(db_client);
                const auto outcome = repository.create_inspection_year(bridge_id, inspection_year);
                if (outcome.status == db::CreateInspectionYearStatus::BridgeNotFound) {
                    respond_workspace_not_found(callback, WorkspaceResource::Bridge);
                    return;
                }
                if (outcome.status == db::CreateInspectionYearStatus::AlreadyExists) {
                    respond_json(
                        callback,
                        inspection_year_already_exists_body(
                            inspection_year, *outcome.existing_inspection_year_id),
                        drogon::k409Conflict
                    );
                    return;
                }

                Json::Value body;
                body["inspection_year"] = outcome.inspection_year->to_json();
                respond_json(callback, body, drogon::k201Created);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );

    drogon::app().registerHandler(
        word_upload_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& inspection_year_id) {
            if (!is_valid_uuid(inspection_year_id)) {
                respond_workspace_not_found(callback, WorkspaceResource::InspectionYear);
                return;
            }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback);
                    return;
                }

                drogon::MultiPartParser parser;
                if (parser.parse(request) != 0) {
                    respond_json(callback, make_error_body("invalid_word_file", "上传内容不是合法的 multipart 表单。"),
                                 drogon::k400BadRequest);
                    return;
                }
                const auto& files = parser.getFiles();
                if (files.size() != 1 || files[0].getItemName() != "file") {
                    respond_json(callback, make_error_body("invalid_word_file", "必须上传一个名为 file 的 Word 文件。"),
                                 drogon::k400BadRequest);
                    return;
                }
                const auto source_type = parser.getParameter<std::string>("source_type");
                if (!is_supported_word_source_type(source_type)) {
                    respond_json(callback,
                                 make_error_body("invalid_source_type", "Word 来源类型必须是软件导出Word或正式Word。"),
                                 drogon::k400BadRequest);
                    return;
                }
                const auto content = files[0].fileContent();
                const auto validation = archive::validate_word_input(
                    files[0].getFileName(), content, config.word_upload_max_bytes);
                if (validation.error == archive::WordInputValidationError::FileTooLarge) {
                    respond_json(callback, make_error_body("word_file_too_large", "Word 文件超过允许的上传大小。"),
                                 drogon::k413RequestEntityTooLarge);
                    return;
                }
                if (!validation.ok()) {
                    respond_json(callback, make_error_body("invalid_word_file", "只能上传非空的 .docx 文件。"),
                                 drogon::k400BadRequest);
                    return;
                }

                db::WorkspaceRepository repository(db_client);
                const auto outcome = repository.upload_word_import(
                    inspection_year_id, source_type, validation.metadata, content,
                    std::filesystem::absolute(config.archive_root));
                if (outcome.status == db::UploadWordStatus::InspectionYearNotFound) {
                    respond_workspace_not_found(callback, WorkspaceResource::InspectionYear);
                    return;
                }
                if (outcome.status == db::UploadWordStatus::InspectionYearNotCurrent) {
                    respond_json(callback,
                                 make_error_body("inspection_year_not_current", "非当前年度版本不能继续导入资料。"),
                                 drogon::k409Conflict);
                    return;
                }
                if (outcome.status == db::UploadWordStatus::ArchiveFailed) {
                    respond_json(callback, make_error_body("word_archive_failed", "Word 文件归档失败。"),
                                 drogon::k500InternalServerError);
                    return;
                }

                Json::Value body;
                body["import_record"] = outcome.import_record->to_json();
                respond_json(callback, body, drogon::k201Created);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );
}

}  // namespace bridge_report::http
