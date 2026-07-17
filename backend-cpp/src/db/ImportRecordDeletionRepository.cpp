#include "bridge_report/db/ImportRecordDeletionRepository.hpp"

#include <memory>
#include <utility>

#include <json/json.h>

#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

std::string compact(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

template <typename ClientPtr>
std::optional<deletion::ImportRecordDeletionPlan> build_plan(
    const ClientPtr& client,
    const std::string& import_record_id,
    const bool lock_rows
) {
    const auto record = client->execSqlSync(
        lock_rows
            ? "select ir.id::text as id,ir.system_number,ir.import_name,ir.import_status,ir.source_type,"
              "ir.updated_at::text as updated_at,b.id::text as bridge_id,b.system_number as bridge_number,"
              "b.bridge_name,iy.id::text as year_id,iy.inspection_year,iy.version_number,"
              "ir.parsed_result_json::text as parsed_json from import_records ir "
              "join bridges b on b.id=ir.bridge_id join inspection_years iy on iy.id=ir.inspection_year_id "
              "where ir.id=$1::uuid for update of ir"
            : "select ir.id::text as id,ir.system_number,ir.import_name,ir.import_status,ir.source_type,"
              "ir.updated_at::text as updated_at,b.id::text as bridge_id,b.system_number as bridge_number,"
              "b.bridge_name,iy.id::text as year_id,iy.inspection_year,iy.version_number,"
              "ir.parsed_result_json::text as parsed_json from import_records ir "
              "join bridges b on b.id=ir.bridge_id join inspection_years iy on iy.id=ir.inspection_year_id "
              "where ir.id=$1::uuid",
        import_record_id
    );
    if (record.empty()) return std::nullopt;

    deletion::ImportRecordDeletionPlan plan;
    const auto& row = record[0];
    plan.import_record_id = row["id"].as<std::string>();
    plan.import_system_number = row["system_number"].as<std::string>();
    plan.import_name = row["import_name"].as<std::string>();
    plan.import_status = row["import_status"].as<std::string>();
    plan.source_type = row["source_type"].as<std::string>();
    plan.updated_at = row["updated_at"].as<std::string>();
    plan.bridge_id = row["bridge_id"].as<std::string>();
    plan.bridge_system_number = row["bridge_number"].as<std::string>();
    plan.bridge_name = row["bridge_name"].as<std::string>();
    plan.inspection_year_id = row["year_id"].as<std::string>();
    plan.inspection_year = row["inspection_year"].as<int>();
    plan.inspection_version = row["version_number"].as<int>();
    plan.fingerprint_items.push_back("import:" + plan.import_record_id + ":" + plan.updated_at + ":" + plan.import_status);

    const auto counts = client->execSqlSync(
        "select "
        "case when jsonb_typeof(parsed_result_json->'defects')='array' then jsonb_array_length(parsed_result_json->'defects') else 0 end as defects,"
        "case when jsonb_typeof(parsed_result_json->'photos')='array' then jsonb_array_length(parsed_result_json->'photos') else 0 end as photos,"
        "(case when jsonb_typeof(parsed_result_json->'ratings'->'overall')='object' then 1 else 0 end + "
        "case when jsonb_typeof(parsed_result_json->'ratings'->'structure_parts')='array' then jsonb_array_length(parsed_result_json->'ratings'->'structure_parts') else 0 end + "
        "case when jsonb_typeof(parsed_result_json->'ratings'->'evaluation_parts')='array' then jsonb_array_length(parsed_result_json->'ratings'->'evaluation_parts') else 0 end + "
        "case when jsonb_typeof(parsed_result_json->'ratings'->'component_ratings')='array' then jsonb_array_length(parsed_result_json->'ratings'->'component_ratings') else 0 end) as ratings,"
        "(select count(*) from defect_observations where source_import_record_id=$1::uuid) + "
        "(select count(*) from defect_photos where source_import_record_id=$1::uuid) + "
        "(select count(*) from condition_ratings where source_import_record_id=$1::uuid) as facts "
        "from import_records where id=$1::uuid",
        import_record_id
    )[0];
    plan.counts.defects = counts["defects"].as<int>();
    plan.counts.photos = counts["photos"].as<int>();
    plan.counts.parsed_images = plan.counts.photos;
    plan.counts.rating_items = counts["ratings"].as<int>();
    plan.counts.formal_fact_references = counts["facts"].as<int>();
    plan.fingerprint_items.push_back("facts:" + std::to_string(plan.counts.formal_fact_references));

    const auto locks = client->execSqlSync(
        "select l.import_record_id::text as import_record_id,u.username,u.display_name,"
        "l.acquired_at::text as acquired_at,l.expires_at::text as expires_at "
        "from import_record_edit_locks l join users u on u.id=l.user_id "
        "where l.import_record_id=$1::uuid and l.expires_at>now()",
        import_record_id
    );
    for (const auto& lock : locks) {
        plan.active_edit_locks.push_back({
            lock["import_record_id"].as<std::string>(), lock["username"].as<std::string>(),
            lock["display_name"].as<std::string>(), lock["acquired_at"].as<std::string>(),
            lock["expires_at"].as<std::string>()
        });
    }

    const auto sources = client->execSqlSync(
        lock_rows
            ? "select id::text as id,storage_relative_path,status,updated_at::text as updated_at,"
              "active_parse_work_relative_path from import_source_files where import_record_id=$1::uuid for update"
            : "select id::text as id,storage_relative_path,status,updated_at::text as updated_at,"
              "active_parse_work_relative_path from import_source_files where import_record_id=$1::uuid",
        import_record_id
    );
    for (const auto& source : sources) {
        const auto id = source["id"].as<std::string>();
        plan.fingerprint_items.push_back("source:" + id + ":" + source["status"].as<std::string>() + ":" + source["updated_at"].as<std::string>());
        if (source["status"].as<std::string>() != "已删除" && source["status"].as<std::string>() != "已过期") {
            plan.temporary_source_ids_to_delete.push_back(id);
            plan.temporary_source_relative_paths_to_delete.push_back(source["storage_relative_path"].as<std::string>());
        }
        if (!source["active_parse_work_relative_path"].isNull()) {
            plan.parse_work_relative_paths_to_delete.push_back(source["active_parse_work_relative_path"].as<std::string>());
        }
    }
    plan.counts.temporary_word_files_to_delete = static_cast<int>(plan.temporary_source_relative_paths_to_delete.size());
    plan.counts.parse_work_directories_to_delete = static_cast<int>(plan.parse_work_relative_paths_to_delete.size());

    const auto files = client->execSqlSync(
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
        " exists(select 1 from condition_ratings x where x.source_file_id=af.id)"
        ") as deletable from archived_files af join candidates c on c.id=af.id order by af.id",
        import_record_id
    );
    for (const auto& file : files) {
        const auto id = file["id"].as<std::string>();
        const bool deletable = file["deletable"].as<bool>();
        plan.fingerprint_items.push_back("file:" + id + (deletable ? ":delete" : ":retain"));
        if (deletable) {
            plan.archived_file_ids_to_delete.push_back(id);
            plan.archived_file_relative_paths_to_delete.push_back(file["storage_relative_path"].as<std::string>());
        } else {
            ++plan.counts.shared_files_retained;
        }
    }
    plan.counts.archived_files_to_delete = static_cast<int>(plan.archived_file_ids_to_delete.size());
    if (lock_rows && !plan.archived_file_ids_to_delete.empty()) {
        for (const auto& id : plan.archived_file_ids_to_delete) {
            client->execSqlSync("select id from archived_files where id=$1::uuid for update", id);
        }
    }
    return plan;
}

}  // namespace

ImportRecordDeletionRepository::ImportRecordDeletionRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

std::optional<deletion::ImportRecordDeletionPlan> ImportRecordDeletionRepository::preview(
    const std::string& import_record_id
) const {
    return build_plan(db_client_, import_record_id, false);
}

deletion::DeleteImportRecordOutcome ImportRecordDeletionRepository::delete_import_record(
    const std::string& import_record_id,
    const std::string& expected_impact_token,
    const std::string& reason,
    const deletion::DeletionActorSnapshot& actor
) {
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    const auto rollback = [&]() { if (tx) { try { tx->rollback(); } catch (...) {} } };
    try {
        tx = db_client_->newTransaction(latch->callback());
        auto plan = build_plan(tx, import_record_id, true);
        if (!plan.has_value()) { rollback(); return {deletion::DeleteImportRecordStatus::NotFound}; }

        deletion::DeleteImportRecordOutcome outcome;
        outcome.current_plan = plan;
        if (!plan->status_allows_delete()) {
            rollback(); outcome.status = deletion::DeleteImportRecordStatus::NotDeletable; return outcome;
        }
        if (!plan->active_edit_locks.empty()) {
            rollback(); outcome.status = deletion::DeleteImportRecordStatus::Locked; return outcome;
        }
        if (plan->counts.formal_fact_references > 0) {
            rollback(); outcome.status = deletion::DeleteImportRecordStatus::HasFormalFacts; return outcome;
        }
        if (plan->impact_token() != expected_impact_token) {
            rollback(); outcome.status = deletion::DeleteImportRecordStatus::ImpactChanged; return outcome;
        }

        const auto audit = tx->execSqlSync(
            "insert into import_record_deletion_audits("
            "original_import_record_id,import_system_number_snapshot,import_name_snapshot,import_status_snapshot,"
            "source_type_snapshot,bridge_id,bridge_system_number_snapshot,bridge_name_snapshot,inspection_year_id,"
            "inspection_year_snapshot,inspection_version_snapshot,actor_user_id,actor_username_snapshot,"
            "actor_display_name_snapshot,reason,impact_json,deleted_counts_json) "
            "values($1::uuid,$2,$3,$4,$5,$6::uuid,$7,$8,$9::uuid,$10,$11,$12::uuid,$13,$14,$15,$16::jsonb,$17::jsonb) "
            "returning id::text as id",
            plan->import_record_id, plan->import_system_number, plan->import_name, plan->import_status,
            plan->source_type, plan->bridge_id, plan->bridge_system_number, plan->bridge_name,
            plan->inspection_year_id, plan->inspection_year, plan->inspection_version,
            actor.user_id, actor.username, actor.display_name, reason,
            compact(plan->to_public_json()), compact(plan->counts.to_json())
        );
        const auto audit_id = audit[0]["id"].as<std::string>();
        for (const auto& path : plan->archived_file_relative_paths_to_delete) {
            tx->execSqlSync(
                "insert into import_record_file_deletion_queue(import_record_deletion_audit_id,storage_kind,artifact_kind,storage_relative_path) "
                "values($1::uuid,'归档存储','文件',$2)", audit_id, path);
        }
        for (const auto& path : plan->temporary_source_relative_paths_to_delete) {
            tx->execSqlSync(
                "insert into import_record_file_deletion_queue(import_record_deletion_audit_id,storage_kind,artifact_kind,storage_relative_path) "
                "values($1::uuid,'临时Word存储','文件',$2)", audit_id, path);
        }
        for (const auto& path : plan->parse_work_relative_paths_to_delete) {
            tx->execSqlSync(
                "insert into import_record_file_deletion_queue(import_record_deletion_audit_id,storage_kind,artifact_kind,storage_relative_path) "
                "values($1::uuid,'归档存储','解析工作目录',$2)", audit_id, path);
        }
        const int cleanup_count = plan->counts.archived_files_to_delete +
            plan->counts.temporary_word_files_to_delete + plan->counts.parse_work_directories_to_delete;
        if (cleanup_count == 0) {
            tx->execSqlSync(
                "update import_record_deletion_audits set file_cleanup_status='已完成',"
                "file_cleanup_completed_at=now() where id=$1::uuid", audit_id);
        }

        tx->execSqlSync("delete from import_source_files where import_record_id=$1::uuid", import_record_id);
        tx->execSqlSync("delete from import_records where id=$1::uuid", import_record_id);
        for (const auto& file_id : plan->archived_file_ids_to_delete) {
            tx->execSqlSync("delete from archived_files where id=$1::uuid", file_id);
        }

        tx.reset();
        if (!latch->wait()) return {deletion::DeleteImportRecordStatus::Failed};
        outcome.status = deletion::DeleteImportRecordStatus::Deleted;
        outcome.deletion_audit_id = audit_id;
        return outcome;
    } catch (...) {
        rollback();
        return {deletion::DeleteImportRecordStatus::Failed};
    }
}

}  // namespace bridge_report::db
