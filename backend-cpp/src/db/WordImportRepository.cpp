#include "bridge_report/db/WordImportRepository.hpp"

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/db/CommitLatch.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

#include <drogon/orm/Exception.h>
#include <json/json.h>

namespace bridge_report::db {
namespace {

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

std::string parser_member(const Json::Value& data, const char* member) {
    return data["contract"].isObject() && data["contract"][member].isString()
        ? data["contract"][member].asString() : std::string();
}

}  // namespace

WordImportRepository::WordImportRepository(drogon::orm::DbClientPtr db_client) : db_client_(std::move(db_client)) {}

std::optional<WordImportContext> WordImportRepository::load_context(
    const std::string& import_record_id,
    const std::filesystem::path& archive_root
) {
    const auto rows = db_client_->execSqlSync(
        "select ir.id::text as import_record_id, ir.bridge_id::text as bridge_id, "
        "ir.inspection_year_id::text as inspection_year_id, ir.system_number as import_number, "
        "ir.import_name, ir.source_type, ir.import_status, b.system_number as bridge_number, "
        "b.bridge_name, iy.inspection_year, af.system_number as file_number, af.storage_relative_path "
        "from import_records ir join bridges b on b.id = ir.bridge_id "
        "left join inspection_years iy on iy.id = ir.inspection_year_id "
        "join archived_files af on af.id = ir.main_file_id and af.bridge_id = ir.bridge_id "
        "and (af.inspection_year_id is null or af.inspection_year_id = ir.inspection_year_id) "
        "and af.file_type = 'Word文档' where ir.id = $1::uuid",
        import_record_id
    );
    if (rows.empty()) return std::nullopt;
    const auto& row = rows[0];
    if (row["inspection_year"].isNull()) return std::nullopt;
    const auto relative = std::filesystem::path(row["storage_relative_path"].as<std::string>());
    std::filesystem::path word_path;
    try { word_path = archive::resolve_path_under_root(archive_root, relative); }
    catch (const std::exception&) { return std::nullopt; }
    auto extension = word_path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (extension != ".docx" || !std::filesystem::is_regular_file(word_path)) return std::nullopt;

    WordImportContext context;
    context.import_record_id = row["import_record_id"].as<std::string>();
    context.bridge_id = row["bridge_id"].as<std::string>();
    if (!row["inspection_year_id"].isNull()) context.inspection_year_id = row["inspection_year_id"].as<std::string>();
    context.bridge_system_number = row["bridge_number"].as<std::string>();
    context.bridge_name = row["bridge_name"].as<std::string>();
    context.inspection_year = row["inspection_year"].as<int>();
    context.import_record_system_number = row["import_number"].as<std::string>();
    context.import_name = row["import_name"].as<std::string>();
    context.source_type = row["source_type"].as<std::string>();
    context.main_file_system_number = row["file_number"].as<std::string>();
    context.word_path = std::move(word_path);
    return context;
}

bool WordImportRepository::mark_parsing(const std::string& import_record_id) {
    const auto result = db_client_->execSqlSync(
        "update import_records set import_status = '解析中', started_at = now(), error_message = null, updated_at = now() "
        "where id = $1::uuid and import_status in ('已上传', '解析失败', '待校对') returning id",
        import_record_id
    );
    return !result.empty();
}

PersistParseOutcome WordImportRepository::persist_parse_result(
    const std::string& import_record_id,
    const archive::ArchivedPhotoBatch& batch
) {
    PersistParseOutcome outcome;
    std::shared_ptr<drogon::orm::Transaction> tx;
    auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto locked = tx->execSqlSync(
            "select bridge_id::text as bridge_id, inspection_year_id::text as inspection_year_id, import_status "
            "from import_records where id = $1::uuid for update", import_record_id);
        if (locked.empty() || (locked[0]["import_status"].as<std::string>() != "解析中"
            && locked[0]["import_status"].as<std::string>() != "已上传"
            && locked[0]["import_status"].as<std::string>() != "解析失败"
            && locked[0]["import_status"].as<std::string>() != "待校对")) {
            tx->rollback();
            outcome.error_code = "import_record_wrong_status";
            return outcome;
        }
        const auto old = tx->execSqlSync(
            "select af.id::text as id, af.storage_relative_path from import_record_files irf "
            "join archived_files af on af.id = irf.archived_file_id "
            "where irf.import_record_id = $1::uuid and irf.file_role = '附件' for update", import_record_id);
        tx->execSqlSync("delete from import_record_files where import_record_id = $1::uuid and file_role = '附件'", import_record_id);
        for (const auto& row : old) {
            const auto deleted = tx->execSqlSync(
                "delete from archived_files af where af.id = $1::uuid "
                "and not exists (select 1 from import_record_files x where x.archived_file_id = af.id) "
                "and not exists (select 1 from import_records x where x.main_file_id = af.id) "
                "and not exists (select 1 from bridge_aliases x where x.source_file_id = af.id) "
                "and not exists (select 1 from component_aliases x where x.source_file_id = af.id) "
                "and not exists (select 1 from defect_observations x where x.source_file_id = af.id) "
                "and not exists (select 1 from defect_photos x where x.archived_file_id = af.id or x.source_file_id = af.id) "
                "and not exists (select 1 from condition_ratings x where x.source_file_id = af.id) "
                "returning storage_relative_path", row["id"].as<std::string>());
            if (!deleted.empty()) {
                outcome.obsolete_storage_paths.emplace_back(deleted[0]["storage_relative_path"].as<std::string>());
            }
        }

        const auto bridge_id = locked[0]["bridge_id"].as<std::string>();
        const bool has_year = !locked[0]["inspection_year_id"].isNull();
        const auto year_id = has_year ? locked[0]["inspection_year_id"].as<std::string>() : std::string();
        for (const auto& file : batch.files) {
            const auto inserted = tx->execSqlSync(
                "insert into archived_files (bridge_id, inspection_year_id, original_file_name, current_file_name, "
                "storage_relative_path, file_type, file_purpose, file_extension, file_size_bytes, file_hash, source_description) "
                "values ($1::uuid, nullif($2, '')::uuid, $3, $4, $5, '图片', 'Word病害照片', $6, $7, $8, $9) returning id",
                bridge_id, year_id, file.original_file_name, file.current_file_name,
                file.storage_relative_path.generic_string(), file.file_extension,
                static_cast<long long>(file.file_size_bytes), file.sha256, "Word解析抽取：" + file.candidate_id);
            tx->execSqlSync(
                "insert into import_record_files (import_record_id, archived_file_id, file_role, process_status, process_note) "
                "values ($1::uuid, $2::uuid, '附件', '处理成功', $3)",
                import_record_id, inserted[0]["id"].as<std::string>(), "照片候选：" + file.candidate_id);
        }
        tx->execSqlSync(
            "update import_records set parsed_result_json = $2::jsonb, importer_name = $3, importer_version = $4, "
            "import_status = '待校对', finished_at = now(), error_message = null, updated_at = now() where id = $1::uuid",
            import_record_id, compact_json(batch.data), parser_member(batch.data, "parser_name"),
            parser_member(batch.data, "parser_version"));
        tx.reset();
        if (!latch->wait()) {
            outcome.error_code = "db_commit_failed";
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

void WordImportRepository::mark_parse_failed(const std::string& import_record_id, const std::string& message) {
    db_client_->execSqlSync(
        "update import_records set import_status = '解析失败', error_message = $2, finished_at = now(), updated_at = now() "
        "where id = $1::uuid and import_status = '解析中'", import_record_id, message);
}

}  // namespace bridge_report::db
