#include "bridge_report/http/ReportTemplateRoutes.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>

#include <trantor/utils/Logger.h>

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/auth/PasswordHash.hpp"
#include "bridge_report/db/ReportTemplateRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {
namespace {

namespace fs = std::filesystem;

constexpr const char* kValidatePath = "/reports/templates/validate";
constexpr const char* kTemplatesDir = "templates";
constexpr const char* kStagingDir = ".staging";

std::optional<db::AuthUser> require_user(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback) {
    auto user = authenticate_request(db_client, request);
    if (!user.has_value()) respond_unauthorized(callback);
    return user;
}

std::optional<db::AuthUser> require_admin(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback) {
    auto user = require_user(db_client, request, callback);
    if (!user.has_value()) return std::nullopt;
    if (!user->is_admin()) {
        respond_forbidden(callback);
        return std::nullopt;
    }
    return user;
}

void respond_invalid(const HttpCallback& callback, const std::string& message) {
    respond_json(callback, make_error_body("report_template_request_invalid", message),
                 drogon::k400BadRequest);
}

void respond_not_found(const HttpCallback& callback) {
    respond_json(callback, make_error_body("report_template_not_found", "模板不存在。"),
                 drogon::k404NotFound);
}

/// 把仓储的写结果翻成 HTTP 响应。除 Ok 外都是可以向管理员解释的业务结论。
bool respond_write_status(
    const HttpCallback& callback, report::TemplateWriteStatus status) {
    switch (status) {
        case report::TemplateWriteStatus::Ok:
            return false;
        case report::TemplateWriteStatus::NotFound:
            respond_not_found(callback);
            return true;
        case report::TemplateWriteStatus::DuplicateCode:
            respond_json(callback,
                make_error_body("report_template_code_duplicated", "模板代码已被占用。"),
                drogon::k409Conflict);
            return true;
        case report::TemplateWriteStatus::NotValidated:
            respond_json(callback,
                make_error_body("report_template_not_validated",
                    "模板尚未通过契约校验，不能启用或设为默认。"),
                drogon::k409Conflict);
            return true;
        case report::TemplateWriteStatus::Referenced:
            respond_json(callback,
                make_error_body("report_template_referenced",
                    "该模板已被年度报告配置引用，不能删除，只能停用。"),
                drogon::k409Conflict);
            return true;
        case report::TemplateWriteStatus::IsDefaultTemplate:
            respond_json(callback,
                make_error_body("report_template_is_default",
                    "这是当前的默认模板。请先把默认让给别的模板，再停用或删除它。"),
                drogon::k409Conflict);
            return true;
    }
    respond_not_found(callback);
    return true;
}

std::string random_token() {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    std::ostringstream stream;
    stream << std::hex << engine() << engine();
    return stream.str();
}

fs::path staging_root(const fs::path& archive_root) {
    return archive_root / kTemplatesDir / kStagingDir;
}

void remove_quietly(const fs::path& path) {
    std::error_code error;
    fs::remove(path, error);
}

/// 模板元数据。multipart 里用一个 metadata 字段承载 JSON，比摊成十几个表单字段清楚。
struct TemplateMetadata {
    report::ReportTemplateInput input;
};

bool parse_metadata(const std::string& text, report::ReportTemplateInput& input,
                    std::string& error_message) {
    Json::CharReaderBuilder builder;
    Json::Value body;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &body, &errors) || !body.isObject()) {
        error_message = "metadata 不是合法的 JSON 对象。";
        return false;
    }
    if (!body["template_code"].isString() || body["template_code"].asString().empty()) {
        error_message = "metadata 必须包含非空的 template_code。";
        return false;
    }
    if (!body["template_name"].isString() || body["template_name"].asString().empty()) {
        error_message = "metadata 必须包含非空的 template_name。";
        return false;
    }
    input.template_code = body["template_code"].asString();
    input.template_name = body["template_name"].asString();
    if (body["description"].isString()) input.description = body["description"].asString();
    input.contract_type = body["contract_type"].isString()
        ? body["contract_type"].asString()
        : std::string("periodic_inspection_v1");
    input.contract_config = body["contract_config"].isObject()
        ? body["contract_config"]
        : Json::Value(Json::objectValue);
    return true;
}

/// 交给 Python 的校验请求。编号格式与所需角色从模板配置里摊平过去。
Json::Value build_validation_request(
    const fs::path& absolute_template_path,
    const std::string& contract_type,
    const Json::Value& contract_config) {
    Json::Value body;
    body["template_path"] = absolute_template_path.generic_string();
    body["contract_type"] = contract_type;
    body["table_number_formats"] = contract_config["table_number_formats"].isObject()
        ? contract_config["table_number_formats"]
        : Json::Value(Json::objectValue);
    body["required_personnel_roles"] = contract_config["required_personnel_roles"].isArray()
        ? contract_config["required_personnel_roles"]
        : Json::Value(Json::arrayValue);
    return body;
}

/// 校验服务的回调：拿到结论（通过与否 + 明细）或一个失败原因。
using ValidationHandler =
    std::function<void(std::optional<report::TemplateValidationOutcome>)>;

void validate_with_python(
    const config::AppConfig& config,
    const Json::Value& request_body,
    ValidationHandler handler) {
    auto client = drogon::HttpClient::newHttpClient(config.python_tools_base_url);
    auto python_request = drogon::HttpRequest::newHttpJsonRequest(request_body);
    python_request->setMethod(drogon::Post);
    python_request->setPath(kValidatePath);
    client->sendRequest(
        python_request,
        [client, handler = std::move(handler)](
            drogon::ReqResult result, const drogon::HttpResponsePtr& response) {
            if (result != drogon::ReqResult::Ok || !response) {
                handler(std::nullopt);
                return;
            }
            const auto body = response->getJsonObject();
            if (!body) {
                handler(std::nullopt);
                return;
            }
            if (response->statusCode() != drogon::k200OK) {
                // 400 是"这份文件连明细都出不来"（不是 zip、缺 document.xml、包不安全）。
                // 把 Python 的错误码原样带出去，管理员才知道是文件本身的问题。
                report::TemplateValidationOutcome outcome;
                outcome.is_valid = false;
                outcome.result = Json::Value(Json::objectValue);
                outcome.result["status"] = "invalid";
                outcome.result["issues"] = Json::Value(Json::arrayValue);
                Json::Value issue;
                const auto& detail = (*body)["detail"];
                issue["code"] = detail["code"].isString() ? detail["code"].asString()
                                                          : std::string("template_invalid");
                issue["message"] = detail["message"].isString()
                    ? detail["message"].asString()
                    : std::string("模板无法解析。");
                issue["severity"] = "error";
                outcome.result["issues"].append(issue);
                handler(outcome);
                return;
            }
            report::TemplateValidationOutcome outcome;
            outcome.is_valid = (*body)["status"].asString() == "valid";
            outcome.result = *body;
            handler(outcome);
        });
}

void respond_validation_rejected(
    const HttpCallback& callback, const report::TemplateValidationOutcome& outcome) {
    auto error = make_error_body("report_template_invalid",
        "模板未通过契约校验，未做任何改动。请按明细修改后重新上传。");
    error["validation_result"] = outcome.result;
    respond_json(callback, error, drogon::k400BadRequest);
}

void respond_validator_unavailable(const HttpCallback& callback) {
    respond_json(callback,
        make_error_body("report_template_validator_unavailable",
            "模板校验服务未返回结果，模板未做任何改动。"),
        drogon::k502BadGateway);
}

/// 接收 multipart 里的模板文件并落到暂存目录。返回暂存的绝对路径。
struct StagedTemplate {
    fs::path absolute_path;
    std::string original_file_name;
    std::string checksum;
    long long size_bytes{0};
};

std::optional<StagedTemplate> stage_uploaded_template(
    const drogon::HttpRequestPtr& request,
    const config::AppConfig& config,
    const HttpCallback& callback) {
    drogon::MultiPartParser parser;
    if (parser.parse(request) != 0) {
        respond_invalid(callback, "上传内容不是合法的 multipart 表单。");
        return std::nullopt;
    }
    const auto& files = parser.getFiles();
    if (files.size() != 1 || files[0].getItemName() != "file") {
        respond_invalid(callback, "必须上传一个名为 file 的 .docx 模板。");
        return std::nullopt;
    }
    const auto original_name = files[0].getFileName();
    if (fs::path(original_name).extension() != ".docx") {
        respond_invalid(callback, "模板必须是 .docx 文件。");
        return std::nullopt;
    }
    const auto content = files[0].fileContent();
    if (content.empty()) {
        respond_invalid(callback, "上传的模板是空文件。");
        return std::nullopt;
    }
    if (content.size() > config.template_upload_max_bytes) {
        respond_json(callback,
            make_error_body("report_template_too_large", "模板超过允许的大小上限。"),
            drogon::k413RequestEntityTooLarge);
        return std::nullopt;
    }

    StagedTemplate staged;
    staged.original_file_name = original_name;
    staged.size_bytes = static_cast<long long>(content.size());
    staged.checksum = "sha256:" + auth::sha256_hex(std::string(content));

    std::error_code error;
    const auto directory = staging_root(config.archive_root);
    fs::create_directories(directory, error);
    // 必须是真的绝对路径：这个路径要发给 Python 工具服务，而两个服务的工作目录不是
    // 同一个（后端从仓库根启动，Python 从 tools-python 启动）。发一条相对路径过去，
    // 那边报的是"模板文件不存在"，看着像文件坏了，其实是路径解错了根。
    const auto absolute_directory = fs::weakly_canonical(directory, error);
    staged.absolute_path =
        (error ? directory : absolute_directory) / (random_token() + ".docx");
    std::ofstream output(staged.absolute_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        respond_json(callback,
            make_error_body("report_template_staging_failed", "模板暂存失败。"),
            drogon::k500InternalServerError);
        return std::nullopt;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    return staged;
}

/// 把暂存文件挪到模板的正式归档位置，返回相对 archive_root 的路径。
std::optional<std::string> commit_staged_template(
    const config::AppConfig& config,
    const StagedTemplate& staged,
    const std::string& template_code) {
    const auto folder = archive::sanitize_path_part(template_code);
    const fs::path relative = fs::path(kTemplatesDir) / folder /
        (staged.absolute_path.stem().string() + ".docx");
    const auto absolute = config.archive_root / relative;
    std::error_code error;
    fs::create_directories(absolute.parent_path(), error);
    fs::rename(staged.absolute_path, absolute, error);
    if (error) {
        // 跨卷时 rename 会失败，退回复制再删。
        fs::copy_file(staged.absolute_path, absolute,
                      fs::copy_options::overwrite_existing, error);
        if (error) return std::nullopt;
        remove_quietly(staged.absolute_path);
    }
    return relative.generic_string();
}

void remove_archived_file_quietly(const config::AppConfig& config, const std::string& relative) {
    try {
        remove_quietly(archive::resolve_path_under_root(config.archive_root, relative));
    } catch (const std::exception&) {
        LOG_WARN << "报告模板旧文件路径不安全，跳过清理: " << relative;
    }
}

}  // namespace

void register_report_template_routes(
    const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config) {
    const std::string collection_path = "/api/report/templates";
    const std::string item_path = "/api/report/templates/{id}";
    const std::string file_path = "/api/report/templates/{id}/file";
    const std::string enabled_path = "/api/report/templates/{id}/enabled";
    const std::string default_path = "/api/report/templates/{id}/default";

    for (const auto& path : {collection_path, item_path, file_path, enabled_path, default_path}) {
        register_options_handler(path);
    }

    drogon::app().registerHandler(
        collection_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportTemplateRepository repository(db_client);
                Json::Value body;
                body["templates"] = Json::Value(Json::arrayValue);
                // 普通用户只该看到能选的模板；停用和校验不通过的属于管理视图。
                for (const auto& item : repository.list(!user->is_admin())) {
                    body["templates"].append(item.to_json());
                }
                respond_json(callback, body);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        item_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_not_found(callback);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportTemplateRepository repository(db_client);
                const auto item = repository.find(id);
                if (!item.has_value() || (!user->is_admin() && !item->is_enabled)) {
                    respond_not_found(callback);
                    return;
                }
                respond_json(callback, item->to_json());
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    // 上传新模板：暂存 -> 校验 -> 通过才登记。不通过时不写库、删暂存件。
    drogon::app().registerHandler(
        collection_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            const auto user = require_admin(db_client, request, callback);
            if (!user.has_value()) return;

            drogon::MultiPartParser meta_parser;
            report::ReportTemplateInput input;
            std::string message;
            if (meta_parser.parse(request) != 0 ||
                !parse_metadata(meta_parser.getParameter<std::string>("metadata"), input, message)) {
                respond_invalid(callback, message.empty() ? "缺少 metadata 字段。" : message);
                return;
            }
            input.updated_by_user_id = user->id;

            const auto staged = stage_uploaded_template(request, config, callback);
            if (!staged.has_value()) return;

            validate_with_python(
                config,
                build_validation_request(staged->absolute_path, input.contract_type,
                                         input.contract_config),
                [db_client, config, callback, staged = *staged, input](
                    std::optional<report::TemplateValidationOutcome> outcome) {
                    if (!outcome.has_value()) {
                        remove_quietly(staged.absolute_path);
                        respond_validator_unavailable(callback);
                        return;
                    }
                    if (!outcome->is_valid) {
                        remove_quietly(staged.absolute_path);
                        respond_validation_rejected(callback, *outcome);
                        return;
                    }
                    const auto relative =
                        commit_staged_template(config, staged, input.template_code);
                    if (!relative.has_value()) {
                        remove_quietly(staged.absolute_path);
                        respond_json(callback,
                            make_error_body("report_template_staging_failed", "模板归档失败。"),
                            drogon::k500InternalServerError);
                        return;
                    }
                    report::TemplateFileInput file;
                    file.original_file_name = staged.original_file_name;
                    file.storage_relative_path = *relative;
                    file.checksum = staged.checksum;
                    file.size_bytes = staged.size_bytes;

                    db::ReportTemplateRepository repository(db_client);
                    auto status = report::TemplateWriteStatus::NotFound;
                    const auto created = repository.create(input, file, *outcome, status);
                    if (status != report::TemplateWriteStatus::Ok || !created.has_value()) {
                        remove_archived_file_quietly(config, *relative);
                        respond_write_status(callback, status);
                        return;
                    }
                    respond_json(callback, created->to_json(), drogon::k201Created);
                });
        },
        {drogon::Post});

    // 下载当前模板源文件（设计 §21.1）。
    //
    // 管理员要拿它去 Word 里改版式，再作为新版本传回来——这是模板维护的唯一正道，
    // 手工去服务器目录里翻文件既找不到也不该找得到。
    drogon::app().registerHandler(
        file_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_not_found(callback);
                return;
            }
            try {
                const auto user = require_admin(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportTemplateRepository repository(db_client);
                const auto item = repository.find(id);
                const auto relative = repository.find_file_relative_path(id);
                if (!item.has_value() || !relative.has_value()) {
                    respond_not_found(callback);
                    return;
                }
                fs::path absolute;
                try {
                    absolute = archive::resolve_path_under_root(config.archive_root, *relative);
                } catch (const std::exception&) {
                    respond_json(callback,
                        make_error_body("unsafe_archive_path", "模板归档路径不安全。"),
                        drogon::k400BadRequest);
                    return;
                }
                if (!fs::is_regular_file(absolute)) {
                    respond_json(callback,
                        make_error_body("report_template_file_missing",
                            "模板文件在归档里找不到，请重新上传。"),
                        drogon::k409Conflict);
                    return;
                }
                auto response = drogon::HttpResponse::newFileResponse(
                    absolute.string(), "", drogon::CT_CUSTOM,
                    "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
                    request);
                response->addHeader("Content-Disposition",
                    attachment_disposition(item->file_name));
                apply_local_dev_cors_headers(response);
                callback(response);
            } catch (const std::filesystem::filesystem_error&) {
                respond_json(callback,
                    make_error_body("report_template_file_missing", "模板文件无法读取。"),
                    drogon::k409Conflict);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    // 替换当前模板文件。校验不通过时保留旧模板，一个字节都不动（设计 §7.1）。
    drogon::app().registerHandler(
        file_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_not_found(callback);
                return;
            }
            const auto user = require_admin(db_client, request, callback);
            if (!user.has_value()) return;

            db::ReportTemplateRepository repository(db_client);
            std::optional<report::ReportTemplate> existing;
            try {
                existing = repository.find(id);
            } catch (...) {
                respond_db_unavailable(callback);
                return;
            }
            if (!existing.has_value()) {
                respond_not_found(callback);
                return;
            }

            const auto staged = stage_uploaded_template(request, config, callback);
            if (!staged.has_value()) return;

            validate_with_python(
                config,
                build_validation_request(staged->absolute_path, existing->contract_type,
                                         existing->contract_config),
                [db_client, config, callback, staged = *staged, id,
                 code = existing->template_code, actor = user->id](
                    std::optional<report::TemplateValidationOutcome> outcome) {
                    if (!outcome.has_value()) {
                        remove_quietly(staged.absolute_path);
                        respond_validator_unavailable(callback);
                        return;
                    }
                    if (!outcome->is_valid) {
                        remove_quietly(staged.absolute_path);
                        respond_validation_rejected(callback, *outcome);
                        return;
                    }
                    const auto relative = commit_staged_template(config, staged, code);
                    if (!relative.has_value()) {
                        remove_quietly(staged.absolute_path);
                        respond_json(callback,
                            make_error_body("report_template_staging_failed", "模板归档失败。"),
                            drogon::k500InternalServerError);
                        return;
                    }
                    report::TemplateFileInput file;
                    file.original_file_name = staged.original_file_name;
                    file.storage_relative_path = *relative;
                    file.checksum = staged.checksum;
                    file.size_bytes = staged.size_bytes;

                    db::ReportTemplateRepository repository(db_client);
                    const auto replacement =
                        repository.replace_file(id, file, *outcome, actor);
                    if (replacement.status != report::TemplateWriteStatus::Ok) {
                        remove_archived_file_quietly(config, *relative);
                        respond_write_status(callback, replacement.status);
                        return;
                    }
                    // 仓储确认旧文件已无人引用，才轮到磁盘清理（设计 §17.4）。
                    if (replacement.obsolete_storage_relative_path.has_value()) {
                        remove_archived_file_quietly(
                            config, *replacement.obsolete_storage_relative_path);
                    }
                    const auto reloaded = repository.find(id);
                    respond_json(callback, reloaded.has_value() ? reloaded->to_json()
                                                                : Json::Value(Json::objectValue));
                });
        },
        {drogon::Post});

    drogon::app().registerHandler(
        enabled_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_not_found(callback);
                return;
            }
            try {
                const auto user = require_admin(db_client, request, callback);
                if (!user.has_value()) return;
                const auto body = request->getJsonObject();
                if (body == nullptr || !(*body)["is_enabled"].isBool()) {
                    respond_invalid(callback, "请求必须包含布尔字段 is_enabled。");
                    return;
                }
                db::ReportTemplateRepository repository(db_client);
                const auto status =
                    repository.set_enabled(id, (*body)["is_enabled"].asBool(), user->id);
                if (respond_write_status(callback, status)) return;
                const auto item = repository.find(id);
                respond_json(callback, item.has_value() ? item->to_json()
                                                        : Json::Value(Json::objectValue));
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        default_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_not_found(callback);
                return;
            }
            try {
                const auto user = require_admin(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportTemplateRepository repository(db_client);
                if (respond_write_status(callback, repository.set_default(id, user->id))) return;
                const auto item = repository.find(id);
                respond_json(callback, item.has_value() ? item->to_json()
                                                        : Json::Value(Json::objectValue));
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        item_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_not_found(callback);
                return;
            }
            try {
                const auto user = require_admin(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportTemplateRepository repository(db_client);
                std::optional<std::string> obsolete;
                if (respond_write_status(callback, repository.remove(id, obsolete))) return;
                if (obsolete.has_value()) {
                    remove_archived_file_quietly(config, *obsolete);
                }
                Json::Value body;
                body["status"] = "deleted";
                respond_json(callback, body);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Delete});
}

}  // namespace bridge_report::http
