#include "bridge_report/db/WordImportRepository.hpp"

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/archive/SourceDbReference.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/db/RatingTreeRepository.hpp"
#include "bridge_report/resolution/ImportResolutionInitializer.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <utility>

#include <drogon/orm/Exception.h>
#include <json/json.h>
#include <trantor/utils/Logger.h>

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
        "b.bridge_name, iy.inspection_year, sf.system_number as file_number, sf.storage_relative_path "
        "from import_records ir join bridges b on b.id = ir.bridge_id "
        "left join inspection_years iy on iy.id = ir.inspection_year_id "
        "join import_source_files sf on sf.import_record_id = ir.id "
        "where ir.id = $1::uuid and iy.is_current "
        "and sf.status in ('待解析', '解析失败') "
        "and (sf.expires_at is null or sf.expires_at > now())",
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
    // .srcref 是接口同步导入的来源引用文件（指向本机离线库，不是它的副本）；
    // 它与 Word 源文件走同一套状态流转与清理，这里一并放行。
    const bool known_extension = extension == ".docx" ||
        extension == std::string(archive::kSourceDbReferenceExtension);
    if (!known_extension || !std::filesystem::is_regular_file(word_path)) return std::nullopt;

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
    context.source_file_system_number = row["file_number"].as<std::string>();
    context.source_relative_path = relative;
    context.word_path = std::move(word_path);
    return context;
}

bool WordImportRepository::mark_parsing(
    const std::string& import_record_id,
    const std::filesystem::path& active_parse_work_relative_path
) {
    const auto result = db_client_->execSqlSync(
        "with eligible as ("
        " select sf.id from import_source_files sf join import_records ir on ir.id=sf.import_record_id "
        " where ir.id=$1::uuid and ir.import_status in ('已上传','解析失败') "
        " and sf.status in ('待解析','解析失败') and (sf.expires_at is null or sf.expires_at>now()) "
        " for update of sf,ir"
        "), source_update as ("
        " update import_source_files sf set status='解析中',parsing_started_at=now(),expires_at=null,"
        " last_error=null,active_parse_work_relative_path=nullif($2,''),updated_at=now() "
        " from eligible e where sf.id=e.id returning sf.id"
        ") update import_records ir set import_status='解析中',started_at=now(),error_message=null,updated_at=now() "
        "where ir.id=$1::uuid and exists(select 1 from source_update) returning ir.id",
        import_record_id, active_parse_work_relative_path.generic_string()
    );
    return !result.empty();
}

void WordImportRepository::clear_active_parse_work_path(const std::string& import_record_id) {
    db_client_->execSqlSync(
        "update import_source_files set active_parse_work_relative_path=null,updated_at=now() "
        "where import_record_id=$1::uuid and active_parse_work_relative_path is not null",
        import_record_id
    );
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
            "select ir.bridge_id::text as bridge_id, "
            "ir.inspection_year_id::text as inspection_year_id,ir.import_status "
            "from import_records ir where ir.id = $1::uuid for update", import_record_id);
        if (locked.empty()) {
            tx->rollback();
            outcome.error_code = "import_record_deleted";
            outcome.error_message = "导入记录已删除，迟到的解析结果未写入。";
            return outcome;
        }
        if (locked[0]["import_status"].as<std::string>() != "解析中") {
            tx->rollback();
            outcome.error_code = "import_record_wrong_status";
            outcome.error_message = "导入记录已不处于解析中状态。";
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
                // 报告模板当前文件也是一份业务引用（报告设计 §17.4）。外键是 RESTRICT，
                // 不在这里排除的话删除会撞外键失败，而不是安静地跳过。
                "and not exists (select 1 from report_templates t where t.file_id = af.id) "
                "returning storage_relative_path", row["id"].as<std::string>());
            if (!deleted.empty()) {
                outcome.obsolete_storage_paths.emplace_back(deleted[0]["storage_relative_path"].as<std::string>());
            }
        }

        const auto bridge_id = locked[0]["bridge_id"].as<std::string>();
        const bool has_year = !locked[0]["inspection_year_id"].isNull();
        const auto year_id = has_year ? locked[0]["inspection_year_id"].as<std::string>() : std::string();

        // 锁顺序固定为 import_records → inspection_years，与绑定事务一致。
        // 年度锁定版本必须在**年度行锁内**读：上面那句只 for update of ir，锁住导入记录
        // 拦不住别人改年度，而两条导入记录可以关联同一个年度。
        std::optional<std::string> locked_revision_id;
        if (has_year) {
            const auto year_row = tx->execSqlSync(
                "select component_inventory_revision_id::text as inventory_revision_id "
                "from inspection_years where id=$1::uuid for update",
                year_id);
            if (!year_row.empty() && !year_row[0]["inventory_revision_id"].isNull()) {
                locked_revision_id =
                    year_row[0]["inventory_revision_id"].as<std::string>();
            }
        }

        // 只解析一次，构件匹配与评定树匹配共用。此前两者各解析一次，READ COMMITTED 下
        // 同一事务里的两次查询可以落在不同快照，于是构件按 R2、评定树按 R3。
        //
        // 仓库对象一律用临时量：它的构造函数按值收下 DbClientPtr 并一直持有，留一个具名
        // 变量在外层作用域，就会让事务的 shared_ptr 活过下面的 tx.reset()，提交回调
        // 永远不来，最后以 db_commit_failed 超时收场。
        const auto revision = ComponentInventoryRepository(tx)
                                  .resolve_confirmed_revision(bridge_id, locked_revision_id);

        // 年度未锁定而解析出了已确认版本 → 立刻锁上，之后的匹配才有稳定依据。
        // 解析不出版本（桥上没有已确认台账，或年度锁着草稿）不算失败：保持既有降级，
        // 两处匹配共享 nullopt，病害带 defect_component_match_required 警告照常入库。
        if (has_year && !locked_revision_id.has_value() && revision.has_value()) {
            if (!ComponentInventoryRepository(tx).lock_pending_year_revision(
                    year_id, bridge_id, std::nullopt, revision->id)) {
                tx->rollback();
                outcome.error_code = "component_inventory_revision_changed";
                outcome.error_message =
                    "检测年度的构件台账版本已被其他操作锁定，请重试导入。";
                return outcome;
            }
        }

        // 5.0 起构件解析与评分树解析都不写回 parsed_result_json：它们落进 027 建的
        // 关系表，由初始化在**同一事务**里建立（设计 §10）。出现"JSON 已落库但一条组
        // 都没有"的中间态时，界面会把整份导入显示成没有任何构件行，而重跑导入又会撞上
        // candidate 唯一约束。
        //
        // 该桥没有已确认台账版本不算失败：全部组停在 unresolved，病害与照片校对照常。
        const auto initialization = resolution::initialize_import_resolution(
            tx,
            resolution::ImportResolutionInitializationContext{
                import_record_id, bridge_id, has_year ? year_id : std::string(), revision},
            batch.data);
        if (!initialization.success) {
            tx->rollback();
            outcome.error_code = initialization.error_code;
            outcome.error_message = initialization.error_message;
            return outcome;
        }
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
        tx->execSqlSync(
            "update import_source_files set status='待清理',cleanup_reason='解析成功',expires_at=null,"
            "last_error=null,next_cleanup_at=now(),updated_at=now() "
            "where import_record_id=$1::uuid and status='解析中'",
            import_record_id);
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

DiscardFailedImportOutcome WordImportRepository::discard_failed_import(
    const std::string& import_record_id
) {
    DiscardFailedImportOutcome outcome;
    std::shared_ptr<drogon::orm::Transaction> tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = db_client_->newTransaction(latch->callback());
        const auto locked = tx->execSqlSync(
            "select import_status from import_records where id=$1::uuid for update",
            import_record_id);
        if (locked.empty()) {
            tx->rollback();
            outcome.deleted = true;
            return outcome;
        }
        const auto status = locked[0]["import_status"].as<std::string>();
        if (status != "已上传" && status != "解析中" && status != "解析失败") {
            tx->rollback();
            outcome.error_message = "导入记录已进入可校对或正式状态，不能按失败导入自动删除。";
            return outcome;
        }

        const auto sources = tx->execSqlSync(
            "select storage_relative_path,active_parse_work_relative_path "
            "from import_source_files where import_record_id=$1::uuid for update",
            import_record_id);
        for (const auto& source : sources) {
            outcome.temporary_source_paths.emplace_back(
                source["storage_relative_path"].as<std::string>());
            if (!source["active_parse_work_relative_path"].isNull()) {
                outcome.parse_work_paths.emplace_back(
                    source["active_parse_work_relative_path"].as<std::string>());
            }
        }

        const auto files = tx->execSqlSync(
            "with candidates as ("
            " select main_file_id as id from import_records where id=$1::uuid and main_file_id is not null"
            " union select archived_file_id from import_record_files where import_record_id=$1::uuid"
            ") select af.id::text as id,af.storage_relative_path,not("
            " exists(select 1 from import_records x where x.main_file_id=af.id and x.id<>$1::uuid) or"
            " exists(select 1 from import_record_files x where x.archived_file_id=af.id and x.import_record_id<>$1::uuid) or"
            " exists(select 1 from bridge_aliases x where x.source_file_id=af.id) or"
            " exists(select 1 from component_aliases x where x.source_file_id=af.id) or"
            " exists(select 1 from defect_observations x where x.source_file_id=af.id) or"
            " exists(select 1 from defect_photos x where x.archived_file_id=af.id or x.source_file_id=af.id) or"
            " exists(select 1 from condition_ratings x where x.source_file_id=af.id) or"
            // 报告模板当前文件（报告设计 §17.4）。
            " exists(select 1 from report_templates t where t.file_id=af.id)"
            ") as deletable from archived_files af join candidates c on c.id=af.id order by af.id",
            import_record_id);
        std::vector<std::string> archived_file_ids;
        for (const auto& file : files) {
            if (!file["deletable"].as<bool>()) continue;
            archived_file_ids.push_back(file["id"].as<std::string>());
            outcome.archived_file_paths.emplace_back(
                file["storage_relative_path"].as<std::string>());
        }
        for (const auto& file_id : archived_file_ids) {
            tx->execSqlSync("select id from archived_files where id=$1::uuid for update", file_id);
        }

        tx->execSqlSync(
            "delete from import_source_files where import_record_id=$1::uuid",
            import_record_id);
        tx->execSqlSync(
            "delete from import_records where id=$1::uuid",
            import_record_id);
        for (const auto& file_id : archived_file_ids) {
            tx->execSqlSync("delete from archived_files where id=$1::uuid", file_id);
        }

        tx.reset();
        if (!latch->wait()) {
            outcome.error_message = "database commit callback reported failure";
            return outcome;
        }
        outcome.deleted = true;
        return outcome;
    } catch (const std::exception& error) {
        if (tx) {
            try { tx->rollback(); }
            catch (...) {
            }
        }
        outcome.error_message = error.what();
        return outcome;
    }
}

void WordImportRepository::mark_parse_failed(
    const std::string& import_record_id,
    const std::string& message,
    const int retention_hours
) {
    db_client_->execSqlSync(
        "with source_update as ("
        " update import_source_files set status='解析失败',expires_at=now()+make_interval(hours=>$3::int),"
        " parsing_started_at=null,last_error=$2,cleanup_reason=null,next_cleanup_at=null,updated_at=now() "
        " where import_record_id=$1::uuid and status='解析中' returning id"
        ") update import_records set import_status='解析失败',error_message=$2,finished_at=now(),updated_at=now() "
        "where id=$1::uuid and import_status='解析中' and exists(select 1 from source_update)",
        import_record_id, message, retention_hours);
}

void WordImportRepository::mark_source_deleted(const std::string& import_record_id) {
    db_client_->execSqlSync(
        "update import_source_files set status='已删除',deleted_at=now(),last_error=null,"
        "next_cleanup_at=null,updated_at=now() "
        "where import_record_id=$1::uuid and status in ('待清理','清理中','清理失败')",
        import_record_id);
}

void WordImportRepository::mark_source_cleanup_failed(
    const std::string& import_record_id,
    const std::string& message,
    const int retry_after_seconds
) {
    db_client_->execSqlSync(
        "update import_source_files set status='清理失败',last_error=$2,"
        "cleanup_attempt_count=cleanup_attempt_count+1,"
        "next_cleanup_at=now()+make_interval(secs=>$3::int),updated_at=now() "
        "where import_record_id=$1::uuid and status in ('待清理','清理中','清理失败')",
        import_record_id, message, retry_after_seconds);
}

}  // namespace bridge_report::db
