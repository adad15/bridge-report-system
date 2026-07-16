#include "bridge_report/db/BridgeDeletionRepository.hpp"

#include <memory>
#include <utility>

#include <json/json.h>

#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

std::optional<std::string> optional_string(const drogon::orm::Field& field) {
    return field.isNull() ? std::nullopt : std::optional<std::string>(field.as<std::string>());
}

std::string compact(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

template <typename ClientPtr>
std::optional<deletion::BridgeDeletionPlan> build_plan(
    const ClientPtr& client,
    const std::string& bridge_id,
    const bool lock_rows
) {
    const auto bridge = client->execSqlSync(
        lock_rows
            ? "select id::text as id,system_number,bridge_name,route_number,route_name,station_mark,status,"
              "updated_at::text as updated_at from bridges where id=$1::uuid for update"
            : "select id::text as id,system_number,bridge_name,route_number,route_name,station_mark,status,"
              "updated_at::text as updated_at from bridges where id=$1::uuid",
        bridge_id
    );
    if (bridge.empty()) return std::nullopt;

    if (lock_rows) {
        client->execSqlSync("select id from inspection_years where bridge_id=$1::uuid for update", bridge_id);
        client->execSqlSync("select id from import_records where bridge_id=$1::uuid for update", bridge_id);
        client->execSqlSync("select id from bridge_components where bridge_id=$1::uuid for update", bridge_id);
        client->execSqlSync("select id from defect_threads where bridge_id=$1::uuid for update", bridge_id);
        client->execSqlSync("select id from defect_observations where bridge_id=$1::uuid for update", bridge_id);
    }

    deletion::BridgeDeletionPlan plan;
    plan.bridge_id = bridge[0]["id"].as<std::string>();
    plan.bridge_system_number = bridge[0]["system_number"].as<std::string>();
    plan.bridge_name = bridge[0]["bridge_name"].as<std::string>();
    plan.route_number = optional_string(bridge[0]["route_number"]);
    plan.route_name = optional_string(bridge[0]["route_name"]);
    plan.station_mark = optional_string(bridge[0]["station_mark"]);
    plan.status = bridge[0]["status"].as<std::string>();
    plan.fingerprint_items.push_back("bridge:" + plan.bridge_id + ":" +
                                     bridge[0]["updated_at"].as<std::string>());

    const auto counts = client->execSqlSync(
        "select "
        "(select count(distinct inspection_year) from inspection_years where bridge_id=$1::uuid) as years,"
        "(select count(*) from inspection_years where bridge_id=$1::uuid) as versions,"
        "(select count(*) from import_records where bridge_id=$1::uuid) as imports,"
        "(select count(*) from bridge_aliases where bridge_id=$1::uuid) as bridge_aliases,"
        "(select count(*) from bridge_components where bridge_id=$1::uuid) as components,"
        "(select count(*) from component_aliases a join bridge_components c on c.id=a.bridge_component_id where c.bridge_id=$1::uuid) as component_aliases,"
        "(select count(*) from defect_threads where bridge_id=$1::uuid) as threads,"
        "(select count(*) from defect_observations where bridge_id=$1::uuid) as observations,"
        "(select count(*) from defect_measurements m join defect_observations o on o.id=m.defect_observation_id where o.bridge_id=$1::uuid) as measurements,"
        "(select count(*) from defect_photos p join defect_observations o on o.id=p.defect_observation_id where o.bridge_id=$1::uuid) as photos,"
        "(select count(*) from condition_ratings r join inspection_years y on y.id=r.inspection_year_id where y.bridge_id=$1::uuid) as ratings,"
        "(select count(*) from defect_comparisons where bridge_id=$1::uuid) as comparisons",
        bridge_id
    )[0];
    plan.counts.inspection_years = counts["years"].as<int>();
    plan.counts.inspection_versions = counts["versions"].as<int>();
    plan.counts.import_records = counts["imports"].as<int>();
    plan.counts.bridge_aliases = counts["bridge_aliases"].as<int>();
    plan.counts.bridge_components = counts["components"].as<int>();
    plan.counts.component_aliases = counts["component_aliases"].as<int>();
    plan.counts.defect_threads = counts["threads"].as<int>();
    plan.counts.defect_observations = counts["observations"].as<int>();
    plan.counts.defect_measurements = counts["measurements"].as<int>();
    plan.counts.defect_photos = counts["photos"].as<int>();
    plan.counts.condition_ratings = counts["ratings"].as<int>();
    plan.counts.defect_comparisons = counts["comparisons"].as<int>();

    const auto temporary_sources = client->execSqlSync(
        lock_rows
            ? "select sf.id::text as id,sf.storage_relative_path,sf.updated_at::text as updated_at "
              "from import_source_files sf join import_records ir on ir.id=sf.import_record_id "
              "where ir.bridge_id=$1::uuid and sf.status not in ('已删除','已过期') "
              "order by sf.id for update of sf"
            : "select sf.id::text as id,sf.storage_relative_path,sf.updated_at::text as updated_at "
              "from import_source_files sf join import_records ir on ir.id=sf.import_record_id "
              "where ir.bridge_id=$1::uuid and sf.status not in ('已删除','已过期') order by sf.id",
        bridge_id);
    for (const auto& row : temporary_sources) {
        const auto id = row["id"].as<std::string>();
        plan.temporary_source_file_ids_to_delete.push_back(id);
        plan.temporary_source_relative_paths_to_delete.push_back(
            row["storage_relative_path"].as<std::string>());
        plan.fingerprint_items.push_back(
            "temporary-source:" + id + ":" + row["updated_at"].as<std::string>());
    }
    plan.counts.temporary_source_files_to_delete = static_cast<int>(temporary_sources.size());

    const auto fingerprints = client->execSqlSync(
        "select item from ("
        "select 'year:'||id::text||':'||updated_at::text as item from inspection_years where bridge_id=$1::uuid "
        "union all select 'import:'||id::text||':'||updated_at::text from import_records where bridge_id=$1::uuid "
        "union all select 'component:'||id::text||':'||updated_at::text from bridge_components where bridge_id=$1::uuid "
        "union all select 'thread:'||id::text||':'||updated_at::text from defect_threads where bridge_id=$1::uuid "
        "union all select 'observation:'||id::text||':'||updated_at::text from defect_observations where bridge_id=$1::uuid "
        "union all select 'comparison:'||id::text||':'||updated_at::text from defect_comparisons where bridge_id=$1::uuid"
        ") x order by item",
        bridge_id
    );
    for (const auto& row : fingerprints) plan.fingerprint_items.push_back(row["item"].as<std::string>());

    const auto locks = client->execSqlSync(
        "select l.import_record_id::text as import_record_id,u.username,u.display_name,"
        "l.acquired_at::text as acquired_at,l.expires_at::text as expires_at "
        "from import_record_edit_locks l join import_records i on i.id=l.import_record_id "
        "join users u on u.id=l.user_id where i.bridge_id=$1::uuid and l.expires_at>now() "
        "order by l.import_record_id",
        bridge_id
    );
    for (const auto& row : locks) {
        plan.active_edit_locks.push_back({
            row["import_record_id"].as<std::string>(), row["username"].as<std::string>(),
            row["display_name"].as<std::string>(), row["acquired_at"].as<std::string>(),
            row["expires_at"].as<std::string>()
        });
    }

    const std::string file_sql =
        "with ty as (select id from inspection_years where bridge_id=$1::uuid),"
        "ti as (select id from import_records where bridge_id=$1::uuid),"
        "tc as (select id from bridge_components where bridge_id=$1::uuid),"
        "to1 as (select id from defect_observations where bridge_id=$1::uuid),"
        "candidate as ("
        "select id from archived_files where bridge_id=$1::uuid or inspection_year_id in(select id from ty) "
        "union select source_file_id from bridge_aliases where bridge_id=$1::uuid "
        "union select source_file_id from component_aliases where bridge_component_id in(select id from tc) "
        "union select main_file_id from import_records where id in(select id from ti) "
        "union select archived_file_id from import_record_files where import_record_id in(select id from ti) "
        "union select source_file_id from defect_observations where id in(select id from to1) "
        "union select archived_file_id from defect_photos where defect_observation_id in(select id from to1) "
        "union select source_file_id from defect_photos where defect_observation_id in(select id from to1) "
        "union select source_file_id from condition_ratings where inspection_year_id in(select id from ty)),"
        "classified as (select af.id,af.storage_relative_path,not("
        "(af.bridge_id is not null and af.bridge_id<>$1::uuid) or "
        "exists(select 1 from inspection_years y where y.id=af.inspection_year_id and y.bridge_id<>$1::uuid) or "
        "exists(select 1 from bridge_aliases a where a.source_file_id=af.id and a.bridge_id<>$1::uuid) or "
        "exists(select 1 from component_aliases a join bridge_components c on c.id=a.bridge_component_id where a.source_file_id=af.id and c.bridge_id<>$1::uuid) or "
        "exists(select 1 from import_records i where i.main_file_id=af.id and i.bridge_id<>$1::uuid) or "
        "exists(select 1 from import_record_files f join import_records i on i.id=f.import_record_id where f.archived_file_id=af.id and i.bridge_id<>$1::uuid) or "
        "exists(select 1 from defect_observations o where o.source_file_id=af.id and o.bridge_id<>$1::uuid) or "
        "exists(select 1 from defect_photos p join defect_observations o on o.id=p.defect_observation_id where (p.archived_file_id=af.id or p.source_file_id=af.id) and o.bridge_id<>$1::uuid) or "
        "exists(select 1 from condition_ratings r join inspection_years y on y.id=r.inspection_year_id where r.source_file_id=af.id and y.bridge_id<>$1::uuid)"
        ") as deletable from archived_files af join candidate c on c.id=af.id) "
        "select id::text as id,storage_relative_path,deletable from classified order by id";
    const auto files = client->execSqlSync(file_sql, bridge_id);
    for (const auto& row : files) {
        const auto id = row["id"].as<std::string>();
        const bool deletable = row["deletable"].as<bool>();
        plan.fingerprint_items.push_back("file:" + id + (deletable ? ":delete" : ":retain"));
        if (deletable) {
            plan.archived_file_ids_to_delete.push_back(id);
            plan.archived_file_relative_paths_to_delete.push_back(
                row["storage_relative_path"].as<std::string>());
        } else {
            ++plan.counts.shared_files_retained;
        }
    }
    plan.counts.archived_files_to_delete =
        static_cast<int>(plan.archived_file_ids_to_delete.size());
    if (lock_rows && !plan.archived_file_ids_to_delete.empty()) {
        client->execSqlSync(
            "select id from archived_files where id=any($1::uuid[]) for update",
            "{" + [&]() { std::string joined; for (std::size_t i=0;i<plan.archived_file_ids_to_delete.size();++i) { if(i) joined+=','; joined+=plan.archived_file_ids_to_delete[i]; } return joined; }() + "}"
        );
    }
    return plan;
}

}  // namespace

BridgeDeletionRepository::BridgeDeletionRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

std::optional<deletion::BridgeDeletionPlan> BridgeDeletionRepository::preview(
    const std::string& bridge_id
) const {
    return build_plan(db_client_, bridge_id, false);
}

deletion::DeleteBridgeOutcome BridgeDeletionRepository::delete_bridge(
    const std::string& bridge_id,
    const std::string& expected_impact_token,
    const std::string& reason,
    const deletion::DeletionActorSnapshot& actor,
    const std::string& batch_id
) {
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    const auto rollback = [&]() { if (tx) { try { tx->rollback(); } catch (...) {} } };
    try {
        tx = db_client_->newTransaction(latch->callback());
        auto plan = build_plan(tx, bridge_id, true);
        if (!plan.has_value()) { rollback(); return {deletion::DeleteBridgeStatus::NotFound}; }
        deletion::DeleteBridgeOutcome outcome;
        outcome.current_plan = plan;
        if (!plan->active_edit_locks.empty()) {
            rollback(); outcome.status = deletion::DeleteBridgeStatus::Locked; return outcome;
        }
        if (plan->impact_token() != expected_impact_token) {
            rollback(); outcome.status = deletion::DeleteBridgeStatus::ImpactChanged; return outcome;
        }
        const auto audit = tx->execSqlSync(
            "insert into bridge_deletion_audits(batch_id,bridge_id,bridge_system_number_snapshot,"
            "bridge_name_snapshot,route_number_snapshot,route_name_snapshot,station_mark_snapshot,status_snapshot,"
            "actor_user_id,actor_username_snapshot,actor_display_name_snapshot,reason,impact_json,deleted_counts_json) "
            "values($1::uuid,$2::uuid,$3,$4,$5,$6,$7,$8,$9::uuid,$10,$11,$12,$13::jsonb,$14::jsonb) returning id::text as id",
            batch_id, plan->bridge_id, plan->bridge_system_number, plan->bridge_name,
            plan->route_number.value_or(""), plan->route_name.value_or(""), plan->station_mark.value_or(""),
            plan->status, actor.user_id, actor.username, actor.display_name, reason,
            compact(plan->to_public_json()), compact(plan->counts.to_json())
        );
        const auto audit_id = audit[0]["id"].as<std::string>();
        for (const auto& path : plan->archived_file_relative_paths_to_delete) {
            tx->execSqlSync(
                "insert into bridge_archived_file_deletion_queue(bridge_deletion_audit_id,storage_relative_path) values($1::uuid,$2)",
                audit_id, path);
        }
        if (plan->archived_file_relative_paths_to_delete.empty()) {
            tx->execSqlSync(
                "update bridge_deletion_audits set file_cleanup_status='已完成',file_cleanup_completed_at=now() where id=$1::uuid",
                audit_id);
        }

        tx->execSqlSync("delete from defect_comparisons where bridge_id=$1::uuid", bridge_id);
        tx->execSqlSync(
            "update import_source_files sf set status='待清理',cleanup_reason='业务删除',"
            "expires_at=null,next_cleanup_at=now(),last_error=null,updated_at=now() "
            "from import_records ir where sf.import_record_id=ir.id and ir.bridge_id=$1::uuid "
            "and sf.status not in ('已删除','已过期')", bridge_id);
        tx->execSqlSync("delete from import_records where bridge_id=$1::uuid", bridge_id);
        tx->execSqlSync(
            "update inspection_years set revision_source_inspection_id=null where revision_source_inspection_id in "
            "(select id from inspection_years where bridge_id=$1::uuid)", bridge_id);
        tx->execSqlSync("delete from inspection_years where bridge_id=$1::uuid", bridge_id);
        tx->execSqlSync("delete from defect_threads where bridge_id=$1::uuid", bridge_id);
        tx->execSqlSync("delete from bridge_components where bridge_id=$1::uuid", bridge_id);
        tx->execSqlSync("delete from bridge_aliases where bridge_id=$1::uuid", bridge_id);
        for (const auto& file_id : plan->archived_file_ids_to_delete) {
            tx->execSqlSync("delete from archived_files where id=$1::uuid", file_id);
        }
        tx->execSqlSync("delete from bridges where id=$1::uuid", bridge_id);

        tx.reset();
        if (!latch->wait()) return {deletion::DeleteBridgeStatus::Failed};
        outcome.status = deletion::DeleteBridgeStatus::Deleted;
        outcome.deletion_audit_id = audit_id;
        return outcome;
    } catch (...) {
        rollback();
        return {deletion::DeleteBridgeStatus::Failed};
    }
}

}  // namespace bridge_report::db
