#include "bridge_report/http/WordImportRoutes.hpp"

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/archive/ExtractedPhotoArchive.hpp"
#include "bridge_report/archive/TemporaryWordStorage.hpp"
#include "bridge_report/contracts/AnnualInspectionContract.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace bridge_report::http {
namespace {

std::string required_string(const Json::Value& body, const char* member) {
    if (!body.isObject() || !body.isMember(member) || !body[member].isString()
        || body[member].asString().empty()) {
        throw std::invalid_argument(std::string(member) + " is required");
    }
    return body[member].asString();
}

}  // namespace

Json::Value build_python_word_request(
    const db::WordImportContext& context,
    const Json::Value& body,
    const std::filesystem::path& temporary_photo_output_dir
) {
    Json::Value request;
    request["docx_path"] = context.word_path.generic_string();
    request["temporary_photo_output_dir"] = temporary_photo_output_dir.generic_string();
    request["rule_profile"] = required_string(body, "rule_profile");
    request["import_mode"] = required_string(body, "import_mode");
    request["source_type"] = context.source_type;
    request["file_role"] = required_string(body, "file_role");
    request["data_role"] = required_string(body, "data_role");
    request["selected_bridge_system_number"] = context.bridge_system_number;
    request["selected_bridge_name"] = context.bridge_name;
    request["inspection_year"] = context.inspection_year;
    request["inspection_date"] = required_string(body, "inspection_date");
    request["report_number"] = required_string(body, "report_number");
    request["project_name"] = required_string(body, "project_name");
    // BridgeAnnualInspectionData 1.1 的兼容字段名仍是 archived_file_system_number；
    // 实际值来自临时来源文件记录，并不表示原 Word 被长期归档。
    request["archived_file_system_number"] = context.source_file_system_number;
    request["import_record_system_number"] = context.import_record_system_number;
    return request;
}

Json::Value extract_python_parse_data(const Json::Value& response_body) {
    if (!response_body.isObject() || !response_body.isMember("data") || !response_body["data"].isObject()) {
        throw std::invalid_argument("Python Word 解析响应缺少 data 对象。");
    }
    return response_body["data"];
}

std::optional<PythonParseError> extract_python_parse_error(const Json::Value& response_body) {
    if (!response_body.isObject() || !response_body.isMember("detail")
        || !response_body["detail"].isObject()) {
        return std::nullopt;
    }
    const auto& detail = response_body["detail"];
    if (!detail.isMember("code") || !detail["code"].isString()
        || !detail.isMember("message") || !detail["message"].isString()) {
        return std::nullopt;
    }
    auto code = detail["code"].asString();
    auto message = detail["message"].asString();
    if (code.empty() || message.empty()) return std::nullopt;
    return PythonParseError{std::move(code), std::move(message)};
}

namespace {

void remove_staging(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::filesystem::remove_all(path, error);
}

void mark_parse_failed_safely(
    const std::shared_ptr<db::WordImportRepository>& repository,
    const std::string& import_record_id,
    const std::string& message,
    const int retention_hours
) noexcept {
    try { repository->mark_parse_failed(import_record_id, message, retention_hours); }
    catch (...) {
    }
}

void cleanup_temporary_source_after_success(
    const std::shared_ptr<db::WordImportRepository>& repository,
    const db::WordImportContext& context,
    const config::AppConfig& config
) noexcept {
    try {
        archive::remove_temporary_word(
            std::filesystem::absolute(config.temporary_word_root), context.source_relative_path);
        repository->mark_source_deleted(context.import_record_id);
    } catch (const std::exception& error) {
        try {
            repository->mark_source_cleanup_failed(
                context.import_record_id, error.what(), config.cleanup_retry_base_seconds);
        } catch (...) {
        }
    }
}

void remove_obsolete_files(
    const std::filesystem::path& archive_root,
    const std::vector<std::filesystem::path>& obsolete,
    const archive::ArchivedPhotoBatch& current
) noexcept {
    std::set<std::string> retained;
    for (const auto& file : current.files) retained.insert(file.storage_relative_path.generic_string());
    for (const auto& relative : obsolete) {
        if (retained.contains(relative.generic_string())) continue;
        try {
            std::error_code error;
            std::filesystem::remove(archive::resolve_path_under_root(archive_root, relative), error);
        } catch (...) {
        }
    }
}

}  // namespace

void register_word_import_routes(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config
) {
    const std::string path = "/api/import-records/{import_record_id}/parse-word";
    register_options_handler(path);
    drogon::app().registerHandler(
        path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& import_record_id) {
            if (!is_valid_uuid(import_record_id)) {
                respond_import_record_not_found(callback);
                return;
            }
            const auto body = request->getJsonObject();
            if (!body) {
                respond_json(callback, make_error_body("invalid_request", "请求体必须是 JSON。"), drogon::k400BadRequest);
                return;
            }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback);
                    return;
                }

                auto repository = std::make_shared<db::WordImportRepository>(db_client);
                const auto archive_root = std::filesystem::absolute(config.archive_root);
                const auto temporary_word_root = std::filesystem::absolute(config.temporary_word_root);
                const auto context = repository->load_context(import_record_id, temporary_word_root);
                if (!context.has_value()) {
                    respond_json(callback, make_error_body("word_import_context_invalid", "导入记录、年度或主 Word 文件不可用。"),
                                 drogon::k409Conflict);
                    return;
                }
                const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                const auto staging_root = archive_root / "work" / "word-import" / (import_record_id + "-" + suffix);
                const auto photo_dir = staging_root / "photos";
                std::filesystem::create_directories(photo_dir);
                Json::Value python_body;
                try { python_body = build_python_word_request(*context, *body, photo_dir); }
                catch (const std::invalid_argument& error) {
                    remove_staging(staging_root);
                    respond_json(callback, make_error_body("invalid_request", error.what()), drogon::k400BadRequest);
                    return;
                }
                if (!repository->mark_parsing(import_record_id)) {
                    remove_staging(staging_root);
                    respond_json(callback, make_error_body("import_record_wrong_status", "当前导入记录不能重新解析。"),
                                 drogon::k409Conflict);
                    return;
                }

                try {
                    auto client = drogon::HttpClient::newHttpClient(config.python_tools_base_url);
                    auto python_request = drogon::HttpRequest::newHttpJsonRequest(python_body);
                    python_request->setMethod(drogon::Post);
                    python_request->setPath("/imports/word/parse");
                    client->sendRequest(
                    python_request,
                    [callback, repository, context = *context, config, archive_root, staging_root, photo_dir](
                        drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
                        if (result != drogon::ReqResult::Ok || !response) {
                            mark_parse_failed_safely(repository, context.import_record_id,
                                                     "Python Word 解析服务调用失败。",
                                                     config.failed_word_retention_hours);
                            remove_staging(staging_root);
                            respond_json(callback, make_error_body("python_parse_failed", "Word 解析服务未返回有效结果。"),
                                         drogon::k502BadGateway);
                            return;
                        }
                        const auto python_response_body = response->getJsonObject();
                        if (response->statusCode() != drogon::k200OK) {
                            if (python_response_body) {
                                if (const auto error = extract_python_parse_error(*python_response_body)) {
                                    mark_parse_failed_safely(repository, context.import_record_id, error->message,
                                                             config.failed_word_retention_hours);
                                    remove_staging(staging_root);
                                    respond_json(callback, make_error_body(error->code, error->message),
                                                 drogon::k400BadRequest);
                                    return;
                                }
                            }
                            mark_parse_failed_safely(repository, context.import_record_id,
                                                     "Python Word 解析服务调用失败。",
                                                     config.failed_word_retention_hours);
                            remove_staging(staging_root);
                            respond_json(callback, make_error_body("python_parse_failed", "Word 解析服务未返回有效结果。"),
                                         drogon::k502BadGateway);
                            return;
                        }
                        if (!python_response_body) {
                            mark_parse_failed_safely(repository, context.import_record_id,
                                                     "Python Word 解析服务调用失败。",
                                                     config.failed_word_retention_hours);
                            remove_staging(staging_root);
                            respond_json(callback, make_error_body("python_parse_failed", "Word 解析服务未返回有效结果。"),
                                         drogon::k502BadGateway);
                            return;
                        }
                        try {
                            auto parsed_data = extract_python_parse_data(*python_response_body);
                            const auto validation = contracts::validate_bridge_annual_inspection_data(parsed_data);
                            if (!validation.ok()) throw std::runtime_error(validation.summary());
                            std::size_t temporary_count = 0;
                            for (const auto& entry : std::filesystem::directory_iterator(photo_dir)) {
                                if (entry.is_regular_file()) ++temporary_count;
                            }
                            archive::PhotoArchiveContext archive_context{
                                photo_dir, archive_root, context.bridge_system_number, "bridge",
                                context.inspection_year, context.import_record_system_number, "import"};
                            auto batch = archive::archive_extracted_photos(parsed_data, archive_context);
                            const auto outcome = repository->persist_parse_result(context.import_record_id, batch);
                            if (!outcome.success) {
                                archive::cleanup_archived_photo_batch(archive_root, batch);
                                mark_parse_failed_safely(repository, context.import_record_id, outcome.error_message,
                                                         config.failed_word_retention_hours);
                                remove_staging(staging_root);
                                respond_json(callback, make_error_body(outcome.error_code, outcome.error_message),
                                             drogon::k500InternalServerError);
                                return;
                            }
                            remove_obsolete_files(archive_root, outcome.obsolete_storage_paths, batch);
                            Json::Value result_body;
                            result_body["parsed"] = true;
                            result_body["temporary_photo_file_count"] = static_cast<Json::UInt64>(temporary_count);
                            result_body["photo_candidate_count"] = static_cast<Json::UInt64>(batch.data["photos"].size());
                            result_body["archived_photo_count"] = static_cast<Json::UInt64>(batch.files.size());
                            cleanup_temporary_source_after_success(repository, context, config);
                            remove_staging(staging_root);
                            respond_json(callback, result_body);
                        } catch (const std::exception& error) {
                            mark_parse_failed_safely(repository, context.import_record_id, error.what(),
                                                     config.failed_word_retention_hours);
                            remove_staging(staging_root);
                            respond_json(callback, make_error_body("word_parse_persistence_failed", error.what()),
                                         drogon::k500InternalServerError);
                        }
                    });
                } catch (const std::exception& error) {
                    mark_parse_failed_safely(repository, context->import_record_id, error.what(),
                                             config.failed_word_retention_hours);
                    remove_staging(staging_root);
                    respond_json(callback, make_error_body("python_request_failed", error.what()),
                                 drogon::k502BadGateway);
                }
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );
}

}  // namespace bridge_report::http
