#include "bridge_report/http/WorkspaceRoutes.hpp"

#include <string>

#include <drogon/drogon.h>
#include <drogon/MultiPart.h>

#include "bridge_report/archive/SourceDbReference.hpp"
#include "bridge_report/archive/WordInputArchive.hpp"
#include "bridge_report/db/WorkspaceRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/WordImportRoutes.hpp"
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

std::string string_member_or_empty(const Json::Value& body, const char* member) {
    if (!body.isObject() || !body.isMember(member) || !body[member].isString()) return {};
    return body[member].asString();
}

std::string utf8_string(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

Json::Value source_db_error_body(const archive::SourceDbValidationError error) {
    switch (error) {
        case archive::SourceDbValidationError::TaskIdMissing:
            return make_error_body(
                "source_task_id_required",
                "需要指定要导入的检测任务；请先在桌面程序里打开该桥，再选择对应任务。");
        case archive::SourceDbValidationError::NotSqlite:
            return make_error_body(
                "source_db_not_readable", "选中的文件不是来源软件的离线库。");
        case archive::SourceDbValidationError::PathMissing:
        case archive::SourceDbValidationError::None:
            break;
    }
    return make_error_body(
        "source_db_not_found",
        "找不到来源软件的离线库文件；请确认路径正确，并已在桌面程序里打开过该桥。");
}

bool is_supported_word_source_type(const std::string& source_type) {
    return source_type == "软件导出Word" || source_type == "正式Word";
}

std::optional<std::string> parse_create_inspection_request(
    const Json::Value& body,
    CreateInspectionRequest& output) {
    if (!body.isObject() || !body.isMember("inspection_year") ||
        !body["inspection_year"].isInt()) {
        return "invalid_inspection_year";
    }
    output.inspection_year = body["inspection_year"].asInt();
    if (output.inspection_year < 1900 || output.inspection_year > 2200) {
        return "invalid_inspection_year";
    }
    if (!body.isMember("rating_tree_version_id") ||
        !body["rating_tree_version_id"].isString() ||
        !is_valid_uuid(body["rating_tree_version_id"].asString())) {
        return "rating_tree_required";
    }
    output.rating_tree_version_id = body["rating_tree_version_id"].asString();
    return std::nullopt;
}

void register_workspace_routes(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config
) {
    const std::string bridge_path = "/api/bridges/{bridge_id}/overview";
    const std::string inspection_path = "/api/inspection-years/{inspection_year_id}/workspace";
    const std::string word_upload_path = "/api/inspection-years/{inspection_year_id}/import-records/word";
    const std::string source_import_path =
        "/api/inspection-years/{inspection_year_id}/import-records/source";
    const std::string source_tasks_path = "/api/source-imports/tasks";
    register_options_handler(bridge_path);
    register_options_handler(inspection_path);
    register_options_handler(word_upload_path);
    register_options_handler(source_import_path);
    register_options_handler(source_tasks_path);

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
                LOG_ERROR << "Bridge overview database failure: " << error.base().what();
                respond_db_unavailable(callback);
            } catch (const std::exception& error) {
                LOG_ERROR << "Bridge overview request handling failure: " << error.what();
                respond_json(callback,
                             make_error_body("bridge_overview_failed", "桥梁概览加载失败，请稍后重试。"),
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
            const auto user = authenticate_request(db_client, request);
            if (!user.has_value()) {
                respond_unauthorized(callback);
                return;
            }
            const auto request_body = request->getJsonObject();
            if (request_body == nullptr) {
                respond_json(callback, make_error_body("invalid_json_body", "请求体不是合法的 JSON。"),
                             drogon::k400BadRequest);
                return;
            }
            CreateInspectionRequest parsed;
            if (const auto parse_error = parse_create_inspection_request(*request_body, parsed)) {
                const auto message = *parse_error == "invalid_inspection_year"
                    ? "检测年度必须是 1900 至 2200 的整数。"
                    : "请选择已发布的桥梁评定树。";
                respond_json(callback, make_error_body(*parse_error, message), drogon::k400BadRequest);
                return;
            }

            try {
                db::WorkspaceRepository repository(db_client);
                const auto outcome = repository.create_inspection_year(
                    bridge_id,
                    parsed.inspection_year,
                    parsed.rating_tree_version_id,
                    user->id);
                if (outcome.status == db::CreateInspectionYearStatus::BridgeNotFound) {
                    respond_workspace_not_found(callback, WorkspaceResource::Bridge);
                    return;
                }
                if (outcome.status == db::CreateInspectionYearStatus::AlreadyExists) {
                    respond_json(
                        callback,
                        inspection_year_already_exists_body(
                            parsed.inspection_year, *outcome.existing_inspection_year_id),
                        drogon::k409Conflict
                    );
                    return;
                }
                if (outcome.status == db::CreateInspectionYearStatus::RatingTreeNotFound) {
                    respond_json(callback,
                                 make_error_body("rating_tree_not_found", "所选评定树不存在。"),
                                 drogon::k400BadRequest);
                    return;
                }
                if (outcome.status == db::CreateInspectionYearStatus::RatingTreeUnavailable) {
                    respond_json(callback,
                                 make_error_body("rating_tree_unavailable", "所选评定树或其底层规范当前不可用。"),
                                 drogon::k409Conflict);
                    return;
                }
                if (outcome.status == db::CreateInspectionYearStatus::Failed) {
                    respond_db_unavailable(callback);
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
                    std::filesystem::absolute(config.temporary_word_root));
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
                if (outcome.status == db::UploadWordStatus::TemporaryStorageFailed) {
                    respond_json(callback, make_error_body("word_temporary_storage_failed", "Word 临时保存失败。"),
                                 drogon::k500InternalServerError);
                    return;
                }

                Json::Value body;
                body["import_record"] = outcome.import_record->to_json();
                respond_json(callback, body, drogon::k201Created);
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
        {drogon::Post}
    );

    // 接口同步导入：不上传离线库本体，只登记一份指向它的引用。
    drogon::app().registerHandler(
        source_import_path,
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
                const auto body = request->getJsonObject();
                if (!body) {
                    respond_json(callback, make_error_body("invalid_request", "请求体必须是 JSON。"),
                                 drogon::k400BadRequest);
                    return;
                }
                archive::SourceDbReference reference{
                    string_member_or_empty(*body, "source_db_path"),
                    string_member_or_empty(*body, "task_id")};
                const auto validation = archive::validate_source_db(reference);
                if (!validation.ok()) {
                    respond_json(callback, source_db_error_body(validation.error),
                                 drogon::k400BadRequest);
                    return;
                }

                const auto content = archive::encode_source_db_reference(reference);
                // 离线库文件名就是个 "1"，拿它当导入名字在列表里根本认不出来。
                // 前端把选中的任务标签（桥名 + 日期 + 条数）一并送来，用它更有用。
                auto display_name = string_member_or_empty(*body, "import_name");
                if (display_name.empty()) display_name = validation.original_file_name;
                const auto metadata = archive::describe_source_db_reference(
                    content, display_name);
                db::WorkspaceRepository repository(db_client);
                const auto outcome = repository.upload_word_import(
                    inspection_year_id, "接口同步", metadata, content,
                    std::filesystem::absolute(config.temporary_word_root));
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
                if (outcome.status == db::UploadWordStatus::TemporaryStorageFailed) {
                    respond_json(callback,
                                 make_error_body("source_reference_storage_failed", "来源引用保存失败。"),
                                 drogon::k500InternalServerError);
                    return;
                }

                Json::Value response;
                response["import_record"] = outcome.import_record->to_json();
                respond_json(callback, response, drogon::k201Created);
            } catch (const drogon::orm::DrogonDbException& error) {
                LOG_ERROR << "Source import database failure: " << error.base().what();
                respond_db_unavailable(callback);
            } catch (const std::exception& error) {
                LOG_ERROR << "Source import request handling failure: " << error.what();
                respond_json(callback,
                             make_error_body("source_import_failed", "来源库导入登记失败，请稍后重试。"),
                             drogon::k500InternalServerError);
            }
        },
        {drogon::Post}
    );

    // 列出离线库里有哪些检测任务。taskId 是厂商库里的 UUID，用户不可能手填，
    // 导入界面得先拿到这张表让人选；路径不填就用来源软件的默认位置。
    drogon::app().registerHandler(
        source_tasks_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback);
                    return;
                }
                const auto body = request->getJsonObject();
                auto requested = body ? string_member_or_empty(*body, "source_db_path")
                                      : std::string{};
                std::filesystem::path resolved;
                if (requested.empty()) {
                    resolved = archive::default_source_db_path();
                } else {
                    resolved = archive::path_from_utf8(requested);
                }
                if (resolved.empty()) {
                    respond_json(callback,
                                 source_db_error_body(archive::SourceDbValidationError::PathMissing),
                                 drogon::k400BadRequest);
                    return;
                }
                const auto resolved_utf8 = utf8_string(resolved);
                // 先本地校验，能立刻说清"文件不在"或"这不是离线库"，不必绕一趟 Python。
                const auto validation = archive::validate_source_db({resolved_utf8, "probe"});
                if (!validation.ok()) {
                    respond_json(callback, source_db_error_body(validation.error),
                                 drogon::k400BadRequest);
                    return;
                }

                Json::Value python_body;
                python_body["source_db_path"] = resolved_utf8;
                auto client = drogon::HttpClient::newHttpClient(config.python_tools_base_url);
                auto python_request = drogon::HttpRequest::newHttpJsonRequest(python_body);
                python_request->setMethod(drogon::Post);
                python_request->setPath("/imports/source/tasks");
                client->sendRequest(
                    python_request,
                    [callback, resolved_utf8](drogon::ReqResult result,
                                              const drogon::HttpResponsePtr& response) mutable {
                        if (result != drogon::ReqResult::Ok || !response) {
                            respond_json(callback,
                                         make_error_body("python_parse_failed", "读取离线库的服务未响应。"),
                                         drogon::k502BadGateway);
                            return;
                        }
                        const auto payload = response->getJsonObject();
                        if (response->statusCode() != drogon::k200OK) {
                            if (payload) {
                                if (const auto error = extract_python_parse_error(*payload)) {
                                    respond_json(callback,
                                                 make_error_body(error->code, error->message),
                                                 drogon::k400BadRequest);
                                    return;
                                }
                            }
                            respond_json(callback,
                                         make_error_body("python_parse_failed", "读取离线库失败。"),
                                         drogon::k502BadGateway);
                            return;
                        }
                        Json::Value out;
                        // 把最终用的路径回给前端，界面上就能显示它到底读的是哪份库。
                        out["source_db_path"] = resolved_utf8;
                        out["tasks"] = payload && payload->isMember("tasks")
                            ? (*payload)["tasks"] : Json::Value(Json::arrayValue);
                        respond_json(callback, out, drogon::k200OK);
                    });
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception& error) {
                LOG_ERROR << "Source task listing failure: " << error.what();
                respond_json(callback,
                             make_error_body("source_import_failed", "读取离线库任务列表失败。"),
                             drogon::k500InternalServerError);
            }
        },
        {drogon::Post}
    );
}

}  // namespace bridge_report::http
