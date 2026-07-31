#include "bridge_report/http/DefectPhotoRoutes.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/ReviewRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/EditLockRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"
#include "bridge_report/review/JsonAccessors.hpp"

namespace bridge_report::http {

namespace {

const char* const kManualPhotoNumberPrefix = "补-";
const char* const kManualCandidatePrefix = "manual_photo_";

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

const Json::Value& photos_of(const Json::Value& draft) {
    static const Json::Value empty(Json::arrayValue);
    return draft.isObject() && draft["photos"].isArray() ? draft["photos"] : empty;
}

/// 从 "补-12" 之类的编号里取出序号；不是人工编号返回 0。
int manual_photo_number_index(const std::string& photo_number) {
    const std::string prefix(kManualPhotoNumberPrefix);
    if (photo_number.rfind(prefix, 0) != 0) return 0;
    const auto digits = photo_number.substr(prefix.size());
    if (digits.empty()) return 0;
    for (const char character : digits) {
        if (character < '0' || character > '9') return 0;
    }
    try { return std::stoi(digits); }
    catch (const std::exception&) { return 0; }
}

int manual_candidate_index(const std::string& candidate_id) {
    const std::string prefix(kManualCandidatePrefix);
    if (candidate_id.rfind(prefix, 0) != 0) return 0;
    const auto digits = candidate_id.substr(prefix.size());
    if (digits.empty()) return 0;
    for (const char character : digits) {
        if (character < '0' || character > '9') return 0;
    }
    try { return std::stoi(digits); }
    catch (const std::exception&) { return 0; }
}

std::string zero_padded(int value) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%04d", value);
    return buffer;
}

/// 事务里锁住导入记录并取回草稿；状态不可编辑时返回空。
std::optional<Json::Value> lock_editable_draft(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const std::string& import_record_id
) {
    const auto locked = tx->execSqlSync(
        "select import_status, parsed_result_json::text as parsed_result_json "
        "from import_records where id=$1::uuid for update",
        import_record_id);
    if (locked.empty()) return std::nullopt;
    if (locked[0]["import_status"].as<std::string>() != "待校对") return std::nullopt;
    return parse_parsed_result_json(locked[0]["parsed_result_json"].as<std::string>());
}

}  // namespace

UploadedPhotoNaming next_uploaded_photo_naming(const Json::Value& draft) {
    int next_number = 0;
    int next_candidate = 0;
    for (const auto& photo : photos_of(draft)) {
        next_number = (std::max)(
            next_number, manual_photo_number_index(review::string_member_or_empty(photo, "photo_number")));
        next_candidate = (std::max)(
            next_candidate, manual_candidate_index(review::string_member_or_empty(photo, "candidate_id")));
    }
    return UploadedPhotoNaming{
        std::string(kManualCandidatePrefix) + zero_padded(next_candidate + 1),
        std::string(kManualPhotoNumberPrefix) + std::to_string(next_number + 1)};
}

Json::Value build_uploaded_photo_candidate(
    const UploadedPhotoNaming& naming,
    const std::string& defect_candidate_id,
    const std::string& original_file_name,
    const std::string& caption,
    const std::string& archive_relative_path
) {
    Json::Value candidate(Json::objectValue);
    candidate["candidate_id"] = naming.candidate_id;
    candidate["photo_number"] = naming.photo_number;
    candidate["linked_defect_candidate_id"] = defect_candidate_id;
    candidate["extracted_file"]["temporary_file_name"] = original_file_name;
    // 图注最终会写进 defect_photos.photo_title，空说明保持 null 而不是空串。
    candidate["extracted_file"]["original_caption"] =
        caption.empty() ? Json::Value() : Json::Value(caption);
    candidate["extracted_file"]["archive_relative_path"] = archive_relative_path;
    // 主动添加即确认：人工上传不是机器推断，两个状态直接到位。
    candidate["match_status"] = "已确认";
    candidate["review_status"] = "已确认";
    candidate["source_ref"]["source_type"] = "manual";
    candidate["confidence"] = 1.0;
    candidate["warnings"] = Json::Value(Json::arrayValue);
    return candidate;
}

bool draft_has_defect_candidate(const Json::Value& draft, const std::string& defect_candidate_id) {
    if (!draft.isObject() || !draft["defects"].isArray()) return false;
    for (const auto& defect : draft["defects"]) {
        if (review::string_member_or_empty(defect, "candidate_id") == defect_candidate_id) return true;
    }
    return false;
}

bool draft_has_photo_naming(const Json::Value& draft, const UploadedPhotoNaming& naming) {
    for (const auto& photo : photos_of(draft)) {
        if (review::string_member_or_empty(photo, "candidate_id") == naming.candidate_id) return true;
        if (review::string_member_or_empty(photo, "photo_number") == naming.photo_number) return true;
    }
    return false;
}

Json::Value take_photo_candidate(Json::Value& draft, const std::string& photo_candidate_id) {
    if (!draft.isObject() || !draft["photos"].isArray()) return Json::Value();
    Json::Value kept(Json::arrayValue);
    Json::Value removed;
    for (const auto& photo : draft["photos"]) {
        if (removed.isNull()
            && review::string_member_or_empty(photo, "candidate_id") == photo_candidate_id) {
            removed = photo;
            continue;
        }
        kept.append(photo);
    }
    if (removed.isNull()) return Json::Value();
    draft["photos"] = kept;
    return removed;
}

UploadedPhotoWriteOutcome insert_uploaded_photo(
    const drogon::orm::DbClientPtr& db_client,
    const review::ImportRecordDetail& detail,
    const archive::ArchivedPhotoFile& file,
    const UploadedPhotoNaming& naming,
    const Json::Value& candidate
) {
    UploadedPhotoWriteOutcome outcome;
    std::shared_ptr<drogon::orm::Transaction> tx;
    auto latch = std::make_shared<db::CommitLatch>();
    try {
        tx = db_client->newTransaction(latch->callback());
        const auto draft = lock_editable_draft(tx, detail.id);
        if (!draft.has_value()) {
            tx->rollback();
            outcome.error_code = "import_record_not_editable";
            outcome.error_message = "导入记录当前不可编辑。";
            return outcome;
        }
        if (!draft_has_defect_candidate(*draft, review::string_member_or_empty(candidate, "linked_defect_candidate_id"))) {
            tx->rollback();
            outcome.error_code = "defect_candidate_not_found";
            outcome.error_message = "目标病害不在本次导入中。";
            return outcome;
        }
        // 编号是在加锁之前算的；加锁后再确认一次没被别人占走。
        if (draft_has_photo_naming(*draft, naming)) {
            tx->rollback();
            outcome.error_code = "photo_candidate_conflict";
            outcome.error_message = "照片编号已被占用，请重试。";
            return outcome;
        }

        const auto inserted = tx->execSqlSync(
            "insert into archived_files (bridge_id, inspection_year_id, original_file_name, current_file_name, "
            "storage_relative_path, file_type, file_purpose, file_extension, file_size_bytes, file_hash, "
            "source_description) values ($1::uuid, nullif($2, '')::uuid, $3, $4, $5, '图片', '人工补充照片', "
            "$6, $7, $8, $9) returning id",
            detail.bridge_id, detail.inspection_year_id.value_or(std::string()),
            file.original_file_name, file.current_file_name,
            file.storage_relative_path.generic_string(), file.file_extension,
            static_cast<long long>(file.file_size_bytes), file.sha256,
            "人工上传：" + naming.candidate_id);
        tx->execSqlSync(
            "insert into import_record_files (import_record_id, archived_file_id, file_role, process_status, "
            "process_note) values ($1::uuid, $2::uuid, '附件', '处理成功', $3)",
            detail.id, inserted[0]["id"].as<std::string>(), "人工补充照片：" + naming.candidate_id);
        // 单语句追加，不做读改写：草稿的其余部分完全不受影响。
        tx->execSqlSync(
            "update import_records set parsed_result_json = jsonb_set(parsed_result_json, '{photos}', "
            "(case when jsonb_typeof(parsed_result_json->'photos') = 'array' "
            "then parsed_result_json->'photos' else '[]'::jsonb end) || $2::jsonb, true), updated_at = now() "
            "where id = $1::uuid and import_status = '待校对'",
            detail.id, compact_json(candidate));

        tx.reset();
        if (!latch->wait()) {
            outcome.error_code = "db_write_failed";
            outcome.error_message = "database commit callback reported failure";
            return outcome;
        }
        outcome.success = true;
        return outcome;
    } catch (const std::exception& error) {
        if (tx) {
            try { tx->rollback(); }
            catch (...) {
            }
        }
        outcome.error_code = "db_write_failed";
        outcome.error_message = error.what();
        return outcome;
    }
}

UploadedPhotoDeleteOutcome delete_uploaded_photo(
    const drogon::orm::DbClientPtr& db_client,
    const std::string& import_record_id,
    const std::string& photo_candidate_id
) {
    UploadedPhotoDeleteOutcome outcome;
    std::shared_ptr<drogon::orm::Transaction> tx;
    auto latch = std::make_shared<db::CommitLatch>();
    try {
        tx = db_client->newTransaction(latch->callback());
        auto draft = lock_editable_draft(tx, import_record_id);
        if (!draft.has_value()) {
            tx->rollback();
            outcome.error_code = "import_record_not_editable";
            outcome.error_message = "导入记录当前不可编辑。";
            return outcome;
        }
        const auto removed = take_photo_candidate(*draft, photo_candidate_id);
        if (removed.isNull()) {
            tx->rollback();
            outcome.error_code = "photo_candidate_not_found";
            outcome.error_message = "指定的照片不在本次导入中。";
            return outcome;
        }
        // Word 抽出的照片的"删除"是纯前端解绑，不走本端点。
        if (review::string_member_or_empty(removed["source_ref"], "source_type") != "manual") {
            tx->rollback();
            outcome.error_code = "photo_not_deletable";
            outcome.error_message = "只有人工上传的照片可以删除。";
            return outcome;
        }
        const auto relative =
            review::string_member_or_empty(removed["extracted_file"], "archive_relative_path");

        tx->execSqlSync(
            "update import_records set parsed_result_json = $2::jsonb, updated_at = now() "
            "where id = $1::uuid and import_status = '待校对'",
            import_record_id, compact_json(*draft));
        if (!relative.empty()) {
            const auto files = tx->execSqlSync(
                "select af.id::text as id, af.file_hash from import_record_files irf "
                "join archived_files af on af.id = irf.archived_file_id "
                "where irf.import_record_id = $1::uuid and af.storage_relative_path = $2",
                import_record_id, relative);
            for (const auto& row : files) {
                tx->execSqlSync(
                    "delete from import_record_files where import_record_id = $1::uuid "
                    "and archived_file_id = $2::uuid",
                    import_record_id, row["id"].as<std::string>());
                tx->execSqlSync("delete from archived_files where id = $1::uuid",
                                row["id"].as<std::string>());
                outcome.sha256 = row["file_hash"].isNull() ? std::string() : row["file_hash"].as<std::string>();
            }
            outcome.storage_relative_path = relative;
        }

        tx.reset();
        if (!latch->wait()) {
            outcome.error_code = "db_write_failed";
            outcome.error_message = "database commit callback reported failure";
            return outcome;
        }
        outcome.success = true;
        return outcome;
    } catch (const std::exception& error) {
        if (tx) {
            try { tx->rollback(); }
            catch (...) {
            }
        }
        outcome.error_code = "db_write_failed";
        outcome.error_message = error.what();
        return outcome;
    }
}

namespace {

void respond_not_editable(const HttpCallback& callback, const std::string& message) {
    respond_json(callback, make_error_body("import_record_not_editable", message), drogon::k409Conflict);
}

/// 待校对 + 非 warnings_only 重开态，才允许改动照片集合。
bool import_record_accepts_new_photos(const review::ImportRecordDetail& detail, const HttpCallback& callback) {
    if (detail.import_status != "待校对") {
        respond_not_editable(callback, "导入记录当前不处于待校对，无法增删照片。");
        return false;
    }
    if (detail.reopened_at.has_value() && detail.reopen_scope.value_or("") == "warnings_only") {
        respond_not_editable(callback, "仅警告范围的重开校对不允许新增或删除照片。");
        return false;
    }
    return true;
}

archive::PhotoArchiveContext photo_archive_context(
    const review::ImportRecordDetail& detail,
    const config::AppConfig& config
) {
    archive::PhotoArchiveContext context;
    context.staging_root = std::filesystem::absolute(config.archive_root);
    context.archive_root = std::filesystem::absolute(config.archive_root);
    context.bridge_system_number = detail.bridge_system_number;
    context.bridge_name = detail.bridge_name;
    context.inspection_year = detail.inspection_year.value_or(0);
    context.import_record_system_number = detail.system_number;
    context.import_name = detail.import_name;
    return context;
}

void handle_upload(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    const std::string& import_record_id
) {
    const auto user = authenticate_request(db_client, request);
    if (!user.has_value()) {
        respond_unauthorized(callback);
        return;
    }
    db::ReviewRepository repository(db_client);
    const auto detail = repository.get_import_record_detail(import_record_id);
    if (!detail.has_value()) {
        respond_import_record_not_found(callback);
        return;
    }
    if (!require_active_edit_lock(db_client, request, import_record_id, *user, callback)) return;
    if (!import_record_accepts_new_photos(*detail, callback)) return;

    drogon::MultiPartParser parser;
    if (parser.parse(request) != 0) {
        respond_json(callback, make_error_body("invalid_photo_file", "上传内容不是合法的 multipart 表单。"),
                     drogon::k400BadRequest);
        return;
    }
    const auto& files = parser.getFiles();
    if (files.size() != 1 || files[0].getItemName() != "file") {
        respond_json(callback, make_error_body("invalid_photo_file", "必须上传一个名为 file 的图片。"),
                     drogon::k400BadRequest);
        return;
    }
    const auto defect_candidate_id = parser.getParameter<std::string>("defect_candidate_id");
    if (defect_candidate_id.empty()) {
        respond_json(callback, make_error_body("defect_candidate_not_found", "必须指定目标病害。"),
                     drogon::k404NotFound);
        return;
    }

    const auto content = files[0].fileContent();
    if (content.size() > config.photo_upload_max_bytes) {
        respond_json(callback, make_error_body("photo_file_too_large", "照片超过允许的大小上限。"),
                     drogon::k413RequestEntityTooLarge);
        return;
    }

    const auto draft = parse_parsed_result_json(detail->parsed_result_json);
    const auto naming = next_uploaded_photo_naming(draft);
    archive::UploadedPhotoInput input;
    input.content = std::string(content);
    input.original_file_name = files[0].getFileName();
    input.candidate_id = naming.candidate_id;
    input.max_bytes = config.photo_upload_max_bytes;

    const auto context = photo_archive_context(*detail, config);
    archive::ArchivedPhotoFile archived;
    try {
        archived = archive::archive_uploaded_photo(input, context);
    } catch (const archive::PhotoTooLargeError&) {
        respond_json(callback, make_error_body("photo_file_too_large", "照片超过允许的大小上限。"),
                     drogon::k413RequestEntityTooLarge);
        return;
    } catch (const std::exception& error) {
        LOG_WARN << "uploaded photo rejected import=" << import_record_id
                 << " detail=" << error.what();
        respond_json(callback,
                     make_error_body("invalid_photo_file", "上传的文件不是受支持的图片，或扩展名与内容不符。"),
                     drogon::k400BadRequest);
        return;
    }

    const auto candidate = build_uploaded_photo_candidate(
        naming, defect_candidate_id, input.original_file_name,
        parser.getParameter<std::string>("caption"),
        archived.storage_relative_path.generic_string());
    const auto written = insert_uploaded_photo(db_client, *detail, archived, naming, candidate);
    if (!written.success) {
        // 库没写成就把刚落盘的文件收掉，不留看不见也删不掉的孤儿。
        if (archived.created_by_batch) {
            archive::remove_archived_photo(context.archive_root, archived.storage_relative_path, archived.sha256);
        }
        if (written.error_code == "defect_candidate_not_found") {
            respond_json(callback, make_error_body(written.error_code, "目标病害不在本次导入中。"),
                         drogon::k404NotFound);
            return;
        }
        if (written.error_code == "import_record_not_editable") {
            respond_not_editable(callback, "导入记录当前不处于待校对，无法新增照片。");
            return;
        }
        if (written.error_code == "photo_candidate_conflict") {
            respond_json(callback, make_error_body(written.error_code, written.error_message),
                         drogon::k409Conflict);
            return;
        }
        LOG_ERROR << "uploaded photo write failed import=" << import_record_id
                  << " detail=" << written.error_message;
        respond_db_unavailable(callback);
        return;
    }

    Json::Value body(Json::objectValue);
    body["photo"] = candidate;
    respond_json(callback, body, drogon::k201Created);
}

void handle_delete(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    const std::string& import_record_id,
    const std::string& photo_candidate_id
) {
    const auto user = authenticate_request(db_client, request);
    if (!user.has_value()) {
        respond_unauthorized(callback);
        return;
    }
    db::ReviewRepository repository(db_client);
    const auto detail = repository.get_import_record_detail(import_record_id);
    if (!detail.has_value()) {
        respond_import_record_not_found(callback);
        return;
    }
    if (!require_active_edit_lock(db_client, request, import_record_id, *user, callback)) return;
    if (!import_record_accepts_new_photos(*detail, callback)) return;

    const auto removed = delete_uploaded_photo(db_client, import_record_id, photo_candidate_id);
    if (!removed.success) {
        if (removed.error_code == "photo_candidate_not_found") {
            respond_json(callback, make_error_body(removed.error_code, removed.error_message),
                         drogon::k404NotFound);
            return;
        }
        if (removed.error_code == "photo_not_deletable" || removed.error_code == "import_record_not_editable") {
            respond_json(callback, make_error_body(removed.error_code, removed.error_message),
                         drogon::k409Conflict);
            return;
        }
        LOG_ERROR << "uploaded photo delete failed import=" << import_record_id
                  << " photo=" << photo_candidate_id << " detail=" << removed.error_message;
        respond_db_unavailable(callback);
        return;
    }
    if (!removed.storage_relative_path.empty()) {
        archive::remove_archived_photo(
            std::filesystem::absolute(config.archive_root), removed.storage_relative_path, removed.sha256);
    }

    Json::Value body(Json::objectValue);
    body["deleted"] = true;
    body["photo_candidate_id"] = photo_candidate_id;
    respond_json(callback, body);
}

}  // namespace

void register_defect_photo_routes(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config
) {
    const std::string collection_path = "/api/import-records/{import_id}/photos";
    const std::string item_path = "/api/import-records/{import_id}/photos/{photo_candidate_id}";
    register_options_handler(collection_path);
    register_options_handler(item_path);

    drogon::app().registerHandler(
        collection_path,
        [db_client, config](const drogon::HttpRequestPtr& request,
                            HttpCallback&& callback,
                            const std::string& import_record_id) {
            if (!is_valid_uuid(import_record_id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                handle_upload(db_client, config, request, callback, import_record_id);
            } catch (const std::exception& error) {
                LOG_ERROR << "defect photo upload failed import=" << import_record_id
                          << " detail=" << error.what();
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        item_path,
        [db_client, config](const drogon::HttpRequestPtr& request,
                            HttpCallback&& callback,
                            const std::string& import_record_id,
                            const std::string& photo_candidate_id) {
            if (!is_valid_uuid(import_record_id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                handle_delete(db_client, config, request, callback, import_record_id, photo_candidate_id);
            } catch (const std::exception& error) {
                LOG_ERROR << "defect photo delete failed import=" << import_record_id
                          << " photo=" << photo_candidate_id << " detail=" << error.what();
                respond_db_unavailable(callback);
            }
        },
        {drogon::Delete});
}

}  // 命名空间 bridge_report::http
