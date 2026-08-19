#include "bridge_report/http/WordImportRoutes.hpp"

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/archive/ExtractedPhotoArchive.hpp"
#include "bridge_report/archive/TemporaryWordStorage.hpp"
#include "bridge_report/contracts/AnnualInspectionContract.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

#include <chrono>
#include <fstream>
#include <iterator>
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

const char* const kSourceDbImportSourceType = "接口同步";

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
    // BridgeAnnualInspectionData 4.0 沿用 archived_file_system_number 字段名；
    // 实际值来自临时来源文件记录，并不表示原 Word 被长期归档。
    request["archived_file_system_number"] = context.source_file_system_number;
    request["import_record_system_number"] = context.import_record_system_number;
    return request;
}

bool is_source_db_import(const db::WordImportContext& context) {
    return context.source_type == kSourceDbImportSourceType;
}

Json::Value build_python_source_request(
    const db::WordImportContext& context,
    const Json::Value& body,
    const archive::SourceDbReference& reference,
    const std::filesystem::path& temporary_photo_output_dir
) {
    Json::Value request;
    // 路径与 taskId 来自导入记录的引用文件，不由本次请求现填——避免同一条导入记录
    // 前后两次解析读到不同的库。
    request["source_db_path"] = reference.source_db_path;
    request["task_id"] = reference.task_id;
    request["temporary_photo_output_dir"] = temporary_photo_output_dir.generic_string();
    request["import_mode"] = required_string(body, "import_mode");
    request["file_role"] = required_string(body, "file_role");
    request["data_role"] = required_string(body, "data_role");
    request["selected_bridge_system_number"] = context.bridge_system_number;
    request["selected_bridge_name"] = context.bridge_name;
    request["inspection_year"] = context.inspection_year;
    request["inspection_date"] = required_string(body, "inspection_date");
    request["report_number"] = required_string(body, "report_number");
    request["project_name"] = required_string(body, "project_name");
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

archive::SourceDbReference read_source_db_reference(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::invalid_argument("来源引用文件不可读。");
    const std::string content(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return archive::decode_source_db_reference(content);
}

void remove_staging(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::filesystem::remove_all(path, error);
}

void finish_staging(
    const std::shared_ptr<db::WordImportRepository>& repository,
    const std::string& import_record_id,
    const std::filesystem::path& path
) noexcept {
    remove_staging(path);
    try { repository->clear_active_parse_work_path(import_record_id); }
    catch (...) {
    }
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

void remove_failed_import_artifacts(
    const db::DiscardFailedImportOutcome& outcome,
    const std::filesystem::path& temporary_word_root,
    const std::filesystem::path& archive_root
) noexcept {
    for (const auto& relative : outcome.temporary_source_paths) {
        try { archive::remove_temporary_word(temporary_word_root, relative); }
        catch (...) {
        }
    }
    for (const auto& relative : outcome.archived_file_paths) {
        try {
            std::error_code error;
            std::filesystem::remove(archive::resolve_path_under_root(archive_root, relative), error);
        } catch (...) {
        }
    }
    for (const auto& relative : outcome.parse_work_paths) {
        try {
            std::error_code error;
            std::filesystem::remove_all(archive::resolve_path_under_root(archive_root, relative), error);
        } catch (...) {
        }
    }
}

bool discard_failed_import_safely(
    const std::shared_ptr<db::WordImportRepository>& word_repository,
    const std::string& import_record_id,
    const std::string& failure_message,
    const config::AppConfig& config,
    const std::filesystem::path& archive_root,
    const std::filesystem::path& staging_root = {}
) noexcept {
    if (!staging_root.empty()) {
        finish_staging(word_repository, import_record_id, staging_root);
    }
    try {
        const auto outcome = word_repository->discard_failed_import(import_record_id);
        if (outcome.deleted) {
            remove_failed_import_artifacts(
                outcome,
                std::filesystem::absolute(config.temporary_word_root),
                archive_root);
            LOG_INFO << "failed import discarded import_record=" << import_record_id;
            return true;
        }
        LOG_ERROR << "failed import could not be discarded import_record=" << import_record_id
                  << " detail=" << outcome.error_message;
    } catch (const std::exception& error) {
        LOG_ERROR << "failed import cleanup raised import_record=" << import_record_id
                  << " detail=" << error.what();
    }
    mark_parse_failed_safely(
        word_repository,
        import_record_id,
        failure_message,
        config.failed_word_retention_hours);
    return false;
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

PersistFailureDisposition disposition_for_persist_failure(const std::string& error_code) {
    // 只有并发抢锁这一类可恢复：解析已经成功，照片也归档了，缺的只是年度版本。
    return error_code == "component_inventory_revision_changed"
        ? PersistFailureDisposition::retain_for_retry
        : PersistFailureDisposition::discard;
}

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
                const auto user = authenticate_request(db_client, request);
                if (!user.has_value()) {
                    respond_unauthorized(callback);
                    return;
                }
                auto repository = std::make_shared<db::WordImportRepository>(db_client);
                const auto archive_root = std::filesystem::absolute(config.archive_root);
                const auto temporary_word_root = std::filesystem::absolute(config.temporary_word_root);
                const auto context = repository->load_context(import_record_id, temporary_word_root);
                if (!context.has_value()) {
                    discard_failed_import_safely(
                        repository, import_record_id,
                        "导入记录、年度或来源文件不可用。", config, archive_root);
                    respond_json(callback, make_error_body("word_import_context_invalid", "导入记录、年度或主 Word 文件不可用。"),
                                 drogon::k409Conflict);
                    return;
                }
                const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                const auto staging_relative = std::filesystem::path("work") / "word-import" /
                    (import_record_id + "-" + suffix);
                const auto staging_root = archive::resolve_path_under_root(archive_root, staging_relative);
                const auto photo_dir = staging_root / "photos";
                std::filesystem::create_directories(photo_dir);
                Json::Value python_body;
                // 两条来源产出同形状的响应，所以这里只决定"调哪个端点、请求体怎么拼"，
                // 之后的照片归档与 parsed_result_json 写入完全共用。
                std::string python_path = "/imports/word/parse";
                try {
                    if (is_source_db_import(*context)) {
                        python_path = "/imports/source/parse";
                        python_body = build_python_source_request(
                            *context, *body, read_source_db_reference(context->word_path),
                            photo_dir);
                    } else {
                        python_body = build_python_word_request(*context, *body, photo_dir);
                    }
                }
                catch (const std::invalid_argument& error) {
                    discard_failed_import_safely(
                        repository, import_record_id,
                        error.what(), config, archive_root, staging_root);
                    respond_json(callback, make_error_body("invalid_request", error.what()), drogon::k400BadRequest);
                    return;
                }
                if (!repository->mark_parsing(import_record_id, staging_relative)) {
                    remove_staging(staging_root);
                    respond_json(callback, make_error_body("import_record_wrong_status", "当前导入记录不能重新解析。"),
                                 drogon::k409Conflict);
                    return;
                }

                try {
                    auto client = drogon::HttpClient::newHttpClient(config.python_tools_base_url);
                    auto python_request = drogon::HttpRequest::newHttpJsonRequest(python_body);
                    python_request->setMethod(drogon::Post);
                    python_request->setPath(python_path);
                    client->sendRequest(
                    python_request,
                    [callback, repository, context = *context, config, archive_root, staging_root, photo_dir](
                        drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
                        const auto fail_parse = [&](const std::string& code, const std::string& message,
                                                    const drogon::HttpStatusCode status) {
                            discard_failed_import_safely(
                                repository, context.import_record_id,
                                message, config, archive_root, staging_root);
                            respond_json(callback, make_error_body(code, message), status);
                        };
                        if (result != drogon::ReqResult::Ok || !response) {
                            fail_parse(
                                "python_parse_failed",
                                "Word 解析服务未返回有效结果。",
                                drogon::k502BadGateway);
                            return;
                        }
                        const auto python_response_body = response->getJsonObject();
                        if (response->statusCode() != drogon::k200OK) {
                            if (python_response_body) {
                                if (const auto error = extract_python_parse_error(*python_response_body)) {
                                    fail_parse(error->code, error->message, drogon::k400BadRequest);
                                    return;
                                }
                            }
                            fail_parse(
                                "python_parse_failed",
                                "Word 解析服务未返回有效结果。",
                                drogon::k502BadGateway);
                            return;
                        }
                        if (!python_response_body) {
                            fail_parse(
                                "python_parse_failed",
                                "Word 解析服务未返回有效结果。",
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
                                // 可恢复的失败绝不能走 fail_parse——那条路会删掉导入记录、
                                // 原始 Word 与归档，把一次重试变成重新上传。
                                //
                                // 也不能只是"不删除"：mark_parsing() 只接受"已上传"/"解析失败"，
                                // 光跳过删除会把记录卡在"解析中"，从此再也重试不了。
                                if (disposition_for_persist_failure(outcome.error_code)
                                    == PersistFailureDisposition::retain_for_retry) {
                                    finish_staging(repository, context.import_record_id, staging_root);
                                    mark_parse_failed_safely(
                                        repository, context.import_record_id,
                                        outcome.error_message,
                                        config.failed_word_retention_hours);
                                    respond_json(
                                        callback,
                                        make_error_body(outcome.error_code, outcome.error_message),
                                        drogon::k409Conflict);
                                    return;
                                }
                                const auto status = outcome.error_code == "import_record_deleted"
                                    || outcome.error_code == "import_record_wrong_status"
                                    ? drogon::k409Conflict : drogon::k500InternalServerError;
                                fail_parse(outcome.error_code, outcome.error_message, status);
                                return;
                            }
                            remove_obsolete_files(archive_root, outcome.obsolete_storage_paths, batch);
                            Json::Value result_body;
                            result_body["parsed"] = true;
                            result_body["temporary_photo_file_count"] = static_cast<Json::UInt64>(temporary_count);
                            result_body["photo_candidate_count"] = static_cast<Json::UInt64>(batch.data["photos"].size());
                            result_body["archived_photo_count"] = static_cast<Json::UInt64>(batch.files.size());
                            cleanup_temporary_source_after_success(repository, context, config);
                            finish_staging(repository, context.import_record_id, staging_root);
                            respond_json(callback, result_body);
                        } catch (const std::exception& error) {
                            fail_parse(
                                "word_parse_persistence_failed",
                                error.what(),
                                drogon::k500InternalServerError);
                        }
                    });
                } catch (const std::exception& error) {
                    discard_failed_import_safely(
                        repository, context->import_record_id,
                        error.what(), config, archive_root, staging_root);
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
