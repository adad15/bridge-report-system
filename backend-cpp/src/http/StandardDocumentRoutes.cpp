#include "bridge_report/http/StandardDocumentRoutes.hpp"

#include <filesystem>
#include <sstream>

#include <drogon/drogon.h>

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/archive/StandardDocumentArchive.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {
namespace {

using db::StandardDocumentRepository;
using db::StandardDocumentWriteOutcome;
using db::StandardDocumentWriteStatus;

bool parse_json_text(const std::string& text, Json::Value& body) {
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(text);
    return Json::parseFromStream(builder, stream, &body, &errors) && body.isObject();
}

void respond_invalid(const HttpCallback& callback, const std::vector<standards::StandardInputIssue>& issues) {
    auto body = make_error_body("standard_document_invalid", "规范信息有误，请按提示修改。");
    body["issues"] = standards::standard_input_issues_json(issues);
    respond_json(callback, body, drogon::k400BadRequest);
}

void respond_error(const HttpCallback& callback, const StandardWriteError& error) {
    respond_json(callback, make_error_body(error.code, error.message), error.status);
}

void respond_document_not_found(const HttpCallback& callback) {
    respond_error(callback, *standard_write_error(StandardDocumentWriteStatus::DocumentNotFound));
}

void respond_version_not_found(const HttpCallback& callback) {
    respond_error(callback, *standard_write_error(StandardDocumentWriteStatus::VersionNotFound));
}

void respond_commit_uncertain(const HttpCallback& callback, const std::string& detail) {
    LOG_ERROR << "Standard document commit requires reconciliation; files retained: " << detail;
    respond_json(callback,
        make_error_body("standard_document_commit_uncertain",
                        "保存结果暂时无法确认，相关文件已保留。请刷新核实后再操作。"),
        drogon::k503ServiceUnavailable);
}

/// 没登录 401，登录了但不是管理员 403。
std::optional<db::AuthUser> require_admin(
    const drogon::orm::DbClientPtr& db_client, const drogon::HttpRequestPtr& request,
    const HttpCallback& callback) {
    const auto user = authenticate_request(db_client, request);
    if (!user.has_value()) {
        respond_unauthorized(callback);
        return std::nullopt;
    }
    if (!user->is_admin()) {
        respond_forbidden(callback);
        return std::nullopt;
    }
    return user;
}

/// multipart 里唯一一个名为 file 的文件；没有就回 standard_file_required。
const drogon::HttpFile* single_file(const drogon::MultiPartParser& parser, const HttpCallback& callback) {
    const auto& files = parser.getFiles();
    if (files.size() != 1 || files[0].getItemName() != "file") {
        respond_json(callback, make_error_body("standard_file_required", "请上传一个 PDF 文件。"),
                     drogon::k400BadRequest);
        return nullptr;
    }
    return &files[0];
}

/// 写盘；失败时已经回应，返回空。
std::optional<archive::ArchivedStandardPdf> archive_upload(
    const drogon::HttpFile& file, const config::AppConfig& config, const std::string& document_id,
    const HttpCallback& callback) {
    try {
        return archive::archive_standard_pdf(file.fileContent(), file.getFileName(), config.archive_root, document_id);
    } catch (const archive::StandardDocumentEmptyError&) {
        respond_json(callback, make_error_body("standard_file_required", "上传的文件是空的。"), drogon::k400BadRequest);
    } catch (const archive::StandardDocumentNotPdfError&) {
        respond_json(callback, make_error_body("standard_file_not_pdf", "只能上传 PDF 文件。"), drogon::k400BadRequest);
    } catch (const std::exception&) {
        respond_json(callback, make_error_body("standard_file_store_failed", "原文写入归档失败，请稍后重试。"),
                     drogon::k500InternalServerError);
    }
    return std::nullopt;
}

db::StandardPdfFileInput file_input(const archive::ArchivedStandardPdf& archived) {
    return {archived.original_file_name, archived.storage_relative_path, archived.file_size_bytes, archived.sha256};
}

void remove_new_file(const config::AppConfig& config, const archive::ArchivedStandardPdf& archived) {
    archive::remove_archived_standard_pdf(config.archive_root, archived.storage_relative_path, archived.sha256);
}

/// 写操作的统一收尾：失败时删掉这次新写的文件；成功时提交后删旧文件，回最新的规范。
void finish_write(const config::AppConfig& config, const HttpCallback& callback,
                  const StandardDocumentWriteOutcome& outcome,
                  const std::optional<archive::ArchivedStandardPdf>& new_file,
                  drogon::HttpStatusCode success_status) {
    if (const auto error = standard_write_error(outcome.status); error.has_value()) {
        if (new_file.has_value()) remove_new_file(config, *new_file);
        respond_error(callback, *error);
        return;
    }
    for (const auto& file : outcome.files_to_remove) {
        archive::remove_archived_standard_pdf(config.archive_root, file.storage_relative_path, file.sha256);
    }
    Json::Value body;
    body["document"] = outcome.document.has_value()
        ? standards::standard_document_json(*outcome.document, standards::local_today())
        : Json::Value(Json::nullValue);
    respond_json(callback, body, success_status);
}

void handle_list(const drogon::orm::DbClientPtr& db_client, const drogon::HttpRequestPtr& request,
                 const HttpCallback& callback) {
    if (!authenticate_request(db_client, request).has_value()) {
        respond_unauthorized(callback);
        return;
    }
    const auto today = standards::local_today();
    Json::Value body;
    body["documents"] = Json::Value(Json::arrayValue);
    for (const auto& document : StandardDocumentRepository(db_client).list()) {
        body["documents"].append(standards::standard_document_json(document, today));
    }
    respond_json(callback, body);
}

void handle_create(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                   const drogon::HttpRequestPtr& request, const HttpCallback& callback) {
    const auto user = require_admin(db_client, request, callback);
    if (!user.has_value()) return;

    drogon::MultiPartParser parser;
    if (parser.parse(request) != 0) {
        respond_invalid(callback, {{"metadata", "上传内容不是合法的 multipart 表单。"}});
        return;
    }
    standards::StandardDocumentInput document;
    standards::StandardVersionInput version;
    const auto issues = parse_create_standard_metadata(parser.getParameter<std::string>("metadata"), document, version);
    if (!issues.empty()) {
        respond_invalid(callback, issues);
        return;
    }
    const auto* file = single_file(parser, callback);
    if (file == nullptr) return;

    const StandardDocumentRepository repository(db_client);
    const auto document_id = repository.allocate_document_id();
    const auto archived = archive_upload(*file, config, document_id, callback);
    if (!archived.has_value()) return;

    StandardDocumentWriteOutcome outcome;
    try {
        outcome = repository.create(document_id, document, version, file_input(*archived), user->id);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    } catch (...) {
        remove_new_file(config, *archived);
        throw;
    }
    finish_write(config, callback, outcome, archived, drogon::k201Created);
}

void handle_update(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                   const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                   const std::string& document_id) {
    if (!is_valid_uuid(document_id)) {
        respond_document_not_found(callback);
        return;
    }
    if (!require_admin(db_client, request, callback).has_value()) return;
    const auto json = request->getJsonObject();
    standards::StandardDocumentInput document;
    const auto issues = json != nullptr
        ? standards::parse_standard_document_input(*json, document)
        : std::vector<standards::StandardInputIssue>{{"body", "请求体必须是 JSON 对象。"}};
    if (!issues.empty()) {
        respond_invalid(callback, issues);
        return;
    }
    StandardDocumentWriteOutcome outcome;
    try {
        outcome = StandardDocumentRepository(db_client).update(document_id, document);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    }
    finish_write(config, callback, outcome, std::nullopt, drogon::k200OK);
}

void handle_remove(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                   const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                   const std::string& document_id) {
    if (!is_valid_uuid(document_id)) {
        respond_document_not_found(callback);
        return;
    }
    if (!require_admin(db_client, request, callback).has_value()) return;
    StandardDocumentWriteOutcome outcome;
    try {
        outcome = StandardDocumentRepository(db_client).remove(document_id);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    }
    if (const auto error = standard_write_error(outcome.status); error.has_value()) {
        respond_error(callback, *error);
        return;
    }
    for (const auto& file : outcome.files_to_remove) {
        archive::remove_archived_standard_pdf(config.archive_root, file.storage_relative_path, file.sha256);
    }
    // 不用 204：前端 request() 一律按 JSON 解析响应体。
    Json::Value body;
    body["deleted"] = true;
    respond_json(callback, body);
}

void handle_add_version(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                        const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                        const std::string& document_id) {
    if (!is_valid_uuid(document_id)) {
        respond_document_not_found(callback);
        return;
    }
    const auto user = require_admin(db_client, request, callback);
    if (!user.has_value()) return;

    drogon::MultiPartParser parser;
    if (parser.parse(request) != 0) {
        respond_invalid(callback, {{"metadata", "上传内容不是合法的 multipart 表单。"}});
        return;
    }
    standards::StandardVersionInput version;
    const auto issues = parse_standard_version_metadata(parser.getParameter<std::string>("metadata"), version);
    if (!issues.empty()) {
        respond_invalid(callback, issues);
        return;
    }
    const auto* file = single_file(parser, callback);
    if (file == nullptr) return;
    const auto archived = archive_upload(*file, config, document_id, callback);
    if (!archived.has_value()) return;

    StandardDocumentWriteOutcome outcome;
    try {
        outcome = StandardDocumentRepository(db_client).add_version(document_id, version, file_input(*archived), user->id);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    } catch (...) {
        remove_new_file(config, *archived);
        throw;
    }
    finish_write(config, callback, outcome, archived, drogon::k201Created);
}

void handle_update_version(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                           const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                           const std::string& version_id) {
    if (!is_valid_uuid(version_id)) {
        respond_version_not_found(callback);
        return;
    }
    if (!require_admin(db_client, request, callback).has_value()) return;
    const auto json = request->getJsonObject();
    standards::StandardVersionInput version;
    const auto issues = json != nullptr
        ? standards::parse_standard_version_input(*json, version)
        : std::vector<standards::StandardInputIssue>{{"body", "请求体必须是 JSON 对象。"}};
    if (!issues.empty()) {
        respond_invalid(callback, issues);
        return;
    }
    StandardDocumentWriteOutcome outcome;
    try {
        outcome = StandardDocumentRepository(db_client).update_version(version_id, version);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    }
    finish_write(config, callback, outcome, std::nullopt, drogon::k200OK);
}

void handle_replace_file(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                         const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                         const std::string& version_id) {
    if (!is_valid_uuid(version_id)) {
        respond_version_not_found(callback);
        return;
    }
    const auto user = require_admin(db_client, request, callback);
    if (!user.has_value()) return;

    drogon::MultiPartParser parser;
    if (parser.parse(request) != 0) {
        respond_invalid(callback, {{"file", "上传内容不是合法的 multipart 表单。"}});
        return;
    }
    const auto* file = single_file(parser, callback);
    if (file == nullptr) return;

    const StandardDocumentRepository repository(db_client);
    const auto current = repository.find_version_file(version_id);
    if (!current.has_value()) {
        respond_version_not_found(callback);
        return;
    }
    const auto archived = archive_upload(*file, config, current->version.document_id, callback);
    if (!archived.has_value()) return;

    StandardDocumentWriteOutcome outcome;
    try {
        outcome = repository.replace_version_file(version_id, file_input(*archived), user->id);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    } catch (...) {
        remove_new_file(config, *archived);
        throw;
    }
    finish_write(config, callback, outcome, archived, drogon::k200OK);
}

void handle_remove_version(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                           const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                           const std::string& version_id) {
    if (!is_valid_uuid(version_id)) {
        respond_version_not_found(callback);
        return;
    }
    if (!require_admin(db_client, request, callback).has_value()) return;
    StandardDocumentWriteOutcome outcome;
    try {
        outcome = StandardDocumentRepository(db_client).remove_version(version_id);
    } catch (const db::StandardDocumentCommitUncertain& error) {
        respond_commit_uncertain(callback, error.what());
        return;
    }
    finish_write(config, callback, outcome, std::nullopt, drogon::k200OK);
}

void handle_content(const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config,
                    const drogon::HttpRequestPtr& request, const HttpCallback& callback,
                    const std::string& version_id) {
    if (!is_valid_uuid(version_id)) {
        respond_version_not_found(callback);
        return;
    }
    const auto found = StandardDocumentRepository(db_client).find_version_file(version_id);
    if (!found.has_value()) {
        respond_version_not_found(callback);
        return;
    }
    const auto respond_missing = [&callback] {
        respond_json(callback, make_error_body("standard_file_missing", "原文文件在归档里找不到，请管理员重新上传。"),
                     drogon::k409Conflict);
    };
    const std::filesystem::path relative(found->version.storage_relative_path);
    if (!archive::is_safe_archive_relative_path(relative)) {
        respond_missing();
        return;
    }
    const auto absolute = archive::resolve_path_under_root(config.archive_root, relative);
    if (!std::filesystem::is_regular_file(absolute)) {
        respond_missing();
        return;
    }
    // 传入 request：drogon 据此处理 Range，大文件可以边下边看。
    auto response = drogon::HttpResponse::newFileResponse(
        absolute.string(), "", drogon::CT_CUSTOM, "application/pdf", request);
    const bool download = request->getParameter("download") == "1";
    response->addHeader("Content-Disposition",
        content_disposition(download ? "attachment" : "inline",
                            standards::standard_pdf_file_name(found->version.code, found->document_name),
                            "standard.pdf"));
    apply_local_dev_cors_headers(response);
    callback(response);
}

}  // namespace

std::vector<standards::StandardInputIssue> parse_create_standard_metadata(
    const std::string& text, standards::StandardDocumentInput& document, standards::StandardVersionInput& version) {
    Json::Value body;
    if (!parse_json_text(text, body)) return {{"metadata", "metadata 不是合法的 JSON 对象。"}};
    auto issues = standards::parse_standard_document_input(body["document"], document, "document.");
    const auto version_issues = standards::parse_standard_version_input(body["version"], version, "version.");
    issues.insert(issues.end(), version_issues.begin(), version_issues.end());
    return issues;
}

std::vector<standards::StandardInputIssue> parse_standard_version_metadata(
    const std::string& text, standards::StandardVersionInput& version) {
    Json::Value body;
    if (!parse_json_text(text, body)) return {{"metadata", "metadata 不是合法的 JSON 对象。"}};
    return standards::parse_standard_version_input(body, version);
}

std::optional<StandardWriteError> standard_write_error(db::StandardDocumentWriteStatus status) {
    switch (status) {
        case StandardDocumentWriteStatus::Ok:
            return std::nullopt;
        case StandardDocumentWriteStatus::DocumentNotFound:
            return StandardWriteError{"standard_document_not_found", "规范不存在或已被删除。", drogon::k404NotFound};
        case StandardDocumentWriteStatus::VersionNotFound:
            return StandardWriteError{"standard_version_not_found", "版本不存在或已被删除。", drogon::k404NotFound};
        case StandardDocumentWriteStatus::NameTaken:
            return StandardWriteError{"standard_document_name_taken", "已有同名规范。", drogon::k409Conflict};
        case StandardDocumentWriteStatus::LabelTaken:
            return StandardWriteError{"standard_version_label_taken", "这部规范里已有同名版本。", drogon::k409Conflict};
        case StandardDocumentWriteStatus::EffectiveDateTaken:
            return StandardWriteError{"standard_version_effective_date_taken",
                                      "这部规范里已有同一实施日期的正式版本。", drogon::k409Conflict};
    }
    return std::nullopt;
}

void register_standard_document_routes(
    const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config) {
    const std::string collection = "/api/standard-documents";
    const std::string document_path = "/api/standard-documents/{document_id}";
    const std::string versions_path = "/api/standard-documents/{document_id}/versions";
    const std::string version_path = "/api/standard-document-versions/{version_id}";
    const std::string file_path = "/api/standard-document-versions/{version_id}/file";
    const std::string content_path = "/api/standard-document-versions/{version_id}/content";
    for (const auto& path : {collection, document_path, versions_path, version_path, file_path, content_path}) {
        register_options_handler(path);
    }

    drogon::app().registerHandler(
        collection,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                handle_list(db_client, request, callback);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        collection,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                handle_create(db_client, config, request, callback);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        document_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& document_id) {
            try {
                handle_update(db_client, config, request, callback, document_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Patch});

    drogon::app().registerHandler(
        document_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& document_id) {
            try {
                handle_remove(db_client, config, request, callback, document_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        versions_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& document_id) {
            try {
                handle_add_version(db_client, config, request, callback, document_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        version_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& version_id) {
            try {
                handle_update_version(db_client, config, request, callback, version_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Patch});

    drogon::app().registerHandler(
        version_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& version_id) {
            try {
                handle_remove_version(db_client, config, request, callback, version_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        file_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& version_id) {
            try {
                handle_replace_file(db_client, config, request, callback, version_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Put});

    drogon::app().registerHandler(
        content_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& version_id) {
            try {
                handle_content(db_client, config, request, callback, version_id);
            } catch (const std::filesystem::filesystem_error&) {
                respond_json(callback, make_error_body("standard_file_missing", "原文文件无法读取。"),
                             drogon::k409Conflict);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});
}

}  // namespace bridge_report::http
