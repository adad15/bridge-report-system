#include "bridge_report/db/InspectionYearDeletionRepository.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <json/json.h>

#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {
namespace {

using TransactionPtr = std::shared_ptr<drogon::orm::Transaction>;

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

template <typename ClientPtr>
std::optional<deletion::InspectionYearDeletionPlan> build_plan(
    const ClientPtr& client,
    const std::string& selected_inspection_year_id,
    bool lock_rows
) {
    const auto selected = client->execSqlSync(
        lock_rows
            ? "select iy.bridge_id::text as bridge_id, iy.inspection_year, b.system_number, b.bridge_name "
              "from inspection_years iy join bridges b on b.id=iy.bridge_id "
              "where iy.id=$1::uuid for update of iy"
            : "select iy.bridge_id::text as bridge_id, iy.inspection_year, b.system_number, b.bridge_name "
              "from inspection_years iy join bridges b on b.id=iy.bridge_id where iy.id=$1::uuid",
        selected_inspection_year_id
    );
    if (selected.empty()) return std::nullopt;

    deletion::InspectionYearDeletionPlan plan;
    plan.bridge_id = selected[0]["bridge_id"].as<std::string>();
    plan.inspection_year = selected[0]["inspection_year"].as<int>();
    plan.bridge_system_number = selected[0]["system_number"].as<std::string>();
    plan.bridge_name = selected[0]["bridge_name"].as<std::string>();

    const auto versions = client->execSqlSync(
        lock_rows
            ? "select id::text as id, version_number, updated_at::text as updated_at "
              "from inspection_years where bridge_id=$1::uuid and inspection_year=$2 "
              "order by version_number for update"
            : "select id::text as id, version_number, updated_at::text as updated_at "
              "from inspection_years where bridge_id=$1::uuid and inspection_year=$2 order by version_number",
        plan.bridge_id, plan.inspection_year
    );
    for (const auto& row : versions) {
        const auto id = row["id"].as<std::string>();
        plan.inspection_year_ids.push_back(id);
        plan.version_numbers.push_back(row["version_number"].as<int>());
        plan.fingerprint_items.push_back("year:" + id + ":" + row["updated_at"].as<std::string>());
    }
    plan.counts.inspection_versions = static_cast<int>(versions.size());

    const auto imports = client->execSqlSync(
        lock_rows
            ? "select ir.id::text as id, ir.updated_at::text as updated_at "
              "from import_records ir join inspection_years iy on iy.id=ir.inspection_year_id "
              "where iy.bridge_id=$1::uuid and iy.inspection_year=$2 order by ir.id for update of ir"
            : "select ir.id::text as id, ir.updated_at::text as updated_at "
              "from import_records ir join inspection_years iy on iy.id=ir.inspection_year_id "
              "where iy.bridge_id=$1::uuid and iy.inspection_year=$2 order by ir.id",
        plan.bridge_id, plan.inspection_year
    );
    for (const auto& row : imports) {
        const auto id = row["id"].as<std::string>();
        plan.import_record_ids.push_back(id);
        plan.fingerprint_items.push_back("import:" + id + ":" + row["updated_at"].as<std::string>());
    }
    plan.counts.import_records = static_cast<int>(imports.size());
    if (lock_rows) {
        client->execSqlSync(
            "select f.id from import_record_files f join import_records ir on ir.id=f.import_record_id "
            "join inspection_years iy on iy.id=ir.inspection_year_id "
            "where iy.bridge_id=$1::uuid and iy.inspection_year=$2 for update of f",
            plan.bridge_id, plan.inspection_year
        );
    }

    const auto locks = client->execSqlSync(
        "select l.import_record_id::text as import_record_id, u.username, u.display_name, "
        "l.acquired_at::text as acquired_at, l.expires_at::text as expires_at "
        "from import_record_edit_locks l join import_records ir on ir.id=l.import_record_id "
        "join inspection_years iy on iy.id=ir.inspection_year_id join users u on u.id=l.user_id "
        "where iy.bridge_id=$1::uuid and iy.inspection_year=$2 and l.expires_at>now() order by l.import_record_id",
        plan.bridge_id, plan.inspection_year
    );
    for (const auto& row : locks) {
        deletion::ActiveDeletionLock lock;
        lock.import_record_id = row["import_record_id"].as<std::string>();
        lock.owner_username = row["username"].as<std::string>();
        lock.owner_display_name = row["display_name"].as<std::string>();
        lock.acquired_at = row["acquired_at"].as<std::string>();
        lock.expires_at = row["expires_at"].as<std::string>();
        plan.active_edit_locks.push_back(std::move(lock));
    }

    const auto observations = client->execSqlSync(
        lock_rows
            ? "select o.id::text as id, o.updated_at::text as updated_at, o.defect_thread_id::text as thread_id "
              "from defect_observations o join inspection_years iy on iy.id=o.inspection_year_id "
              "where iy.bridge_id=$1::uuid and iy.inspection_year=$2 order by o.id for update of o"
            : "select o.id::text as id, o.updated_at::text as updated_at, o.defect_thread_id::text as thread_id "
              "from defect_observations o join inspection_years iy on iy.id=o.inspection_year_id "
              "where iy.bridge_id=$1::uuid and iy.inspection_year=$2 order by o.id",
        plan.bridge_id, plan.inspection_year
    );
    for (const auto& row : observations) {
        const auto id = row["id"].as<std::string>();
        plan.defect_observation_ids.push_back(id);
        plan.fingerprint_items.push_back("observation:" + id + ":" + row["updated_at"].as<std::string>());
        if (!row["thread_id"].isNull()) plan.defect_thread_ids.push_back(row["thread_id"].as<std::string>());
    }
    std::sort(plan.defect_thread_ids.begin(), plan.defect_thread_ids.end());
    plan.defect_thread_ids.erase(std::unique(plan.defect_thread_ids.begin(), plan.defect_thread_ids.end()), plan.defect_thread_ids.end());
    plan.counts.defect_observations = static_cast<int>(observations.size());
    plan.counts.defect_threads_affected = static_cast<int>(plan.defect_thread_ids.size());

    if (lock_rows) {
        client->execSqlSync(
            "select m.id from defect_measurements m join defect_observations o on o.id=m.defect_observation_id "
            "join inspection_years iy on iy.id=o.inspection_year_id "
            "where iy.bridge_id=$1::uuid and iy.inspection_year=$2 for update of m",
            plan.bridge_id, plan.inspection_year
        );
        client->execSqlSync(
            "select p.id from defect_photos p join defect_observations o on o.id=p.defect_observation_id "
            "join inspection_years iy on iy.id=o.inspection_year_id "
            "where iy.bridge_id=$1::uuid and iy.inspection_year=$2 for update of p",
            plan.bridge_id, plan.inspection_year
        );
    }

    const auto detail_counts = client->execSqlSync(
        "select "
        "(select count(*) from defect_measurements m join defect_observations o on o.id=m.defect_observation_id "
        " join inspection_years iy on iy.id=o.inspection_year_id where iy.bridge_id=$1::uuid and iy.inspection_year=$2) as measurements, "
        "(select count(*) from defect_photos p join defect_observations o on o.id=p.defect_observation_id "
        " join inspection_years iy on iy.id=o.inspection_year_id where iy.bridge_id=$1::uuid and iy.inspection_year=$2) as photos",
        plan.bridge_id, plan.inspection_year
    );
    plan.counts.defect_measurements = detail_counts[0]["measurements"].as<int>();
    plan.counts.defect_photos = detail_counts[0]["photos"].as<int>();

    const auto ratings = client->execSqlSync(
        lock_rows
            ? "select r.id::text as id, r.updated_at::text as updated_at from condition_ratings r "
              "join inspection_years iy on iy.id=r.inspection_year_id "
              "where iy.bridge_id=$1::uuid and iy.inspection_year=$2 order by r.id for update of r"
            : "select r.id::text as id, r.updated_at::text as updated_at from condition_ratings r "
              "join inspection_years iy on iy.id=r.inspection_year_id "
              "where iy.bridge_id=$1::uuid and iy.inspection_year=$2 order by r.id",
        plan.bridge_id, plan.inspection_year
    );
    for (const auto& row : ratings) {
        const auto id = row["id"].as<std::string>();
        plan.condition_rating_ids.push_back(id);
        plan.fingerprint_items.push_back("rating:" + id + ":" + row["updated_at"].as<std::string>());
    }
    plan.counts.condition_ratings = static_cast<int>(ratings.size());

    const auto comparisons = client->execSqlSync(
        lock_rows
            ? "select c.id::text as id, c.updated_at::text as updated_at from defect_comparisons c "
              "join inspection_years cy on cy.id=c.current_inspection_year_id "
              "join inspection_years py on py.id=c.compared_inspection_year_id "
              "where (cy.bridge_id=$1::uuid and cy.inspection_year=$2) "
              "or (py.bridge_id=$1::uuid and py.inspection_year=$2) order by c.id for update of c"
            : "select c.id::text as id, c.updated_at::text as updated_at from defect_comparisons c "
              "join inspection_years cy on cy.id=c.current_inspection_year_id "
              "join inspection_years py on py.id=c.compared_inspection_year_id "
              "where (cy.bridge_id=$1::uuid and cy.inspection_year=$2) "
              "or (py.bridge_id=$1::uuid and py.inspection_year=$2) order by c.id",
        plan.bridge_id, plan.inspection_year
    );
    for (const auto& row : comparisons) {
        const auto id = row["id"].as<std::string>();
        plan.defect_comparison_ids.push_back(id);
        plan.fingerprint_items.push_back("comparison:" + id + ":" + row["updated_at"].as<std::string>());
    }
    plan.counts.defect_comparisons = static_cast<int>(comparisons.size());

    // 先锁候选归档行。这样外部资料在本事务提交前不能新增对这些文件的 FK 引用；
    // 随后的“共享/独占”分类与实际删除使用同一稳定快照。
    if (lock_rows) {
        client->execSqlSync(
            "with target_years as (select id from inspection_years where bridge_id=$1::uuid and inspection_year=$2), "
            "target_imports as (select id from import_records where inspection_year_id in (select id from target_years)), "
            "target_obs as (select id from defect_observations where inspection_year_id in (select id from target_years)), "
            "candidate as (select id from archived_files where inspection_year_id in (select id from target_years) "
            " union select main_file_id from import_records where id in (select id from target_imports) "
            " union select archived_file_id from import_record_files where import_record_id in (select id from target_imports) "
            " union select source_file_id from defect_observations where id in (select id from target_obs) "
            " union select archived_file_id from defect_photos where defect_observation_id in (select id from target_obs) "
            " union select source_file_id from defect_photos where defect_observation_id in (select id from target_obs) "
            " union select source_file_id from condition_ratings where inspection_year_id in (select id from target_years)) "
            "select af.id from archived_files af join candidate c on c.id=af.id order by af.id for update of af",
            plan.bridge_id, plan.inspection_year
        );
    }

    // 候选文件来自目标年度自身、导入附件、病害/照片/评分证据；任一年度外引用都会保留。
    const auto files = client->execSqlSync(
        "with target_years as (select id from inspection_years where bridge_id=$1::uuid and inspection_year=$2), "
        "target_imports as (select id from import_records where inspection_year_id in (select id from target_years)), "
        "target_obs as (select id from defect_observations where inspection_year_id in (select id from target_years)), "
        "candidate as ("
        " select id from archived_files where inspection_year_id in (select id from target_years) "
        " union select main_file_id from import_records where id in (select id from target_imports) "
        " union select archived_file_id from import_record_files where import_record_id in (select id from target_imports) "
        " union select source_file_id from defect_observations where id in (select id from target_obs) "
        " union select archived_file_id from defect_photos where defect_observation_id in (select id from target_obs) "
        " union select source_file_id from defect_photos where defect_observation_id in (select id from target_obs) "
        " union select source_file_id from condition_ratings where inspection_year_id in (select id from target_years)"
        "), classified as (select af.id, af.storage_relative_path, not ("
        " exists(select 1 from bridge_aliases x where x.source_file_id=af.id) or "
        " exists(select 1 from component_aliases x where x.source_file_id=af.id) or "
        " exists(select 1 from import_records x where x.main_file_id=af.id and x.id not in (select id from target_imports)) or "
        " exists(select 1 from import_record_files x where x.archived_file_id=af.id and x.import_record_id not in (select id from target_imports)) or "
        " exists(select 1 from defect_observations x where x.source_file_id=af.id and x.id not in (select id from target_obs)) or "
        " exists(select 1 from defect_photos x join defect_observations o on o.id=x.defect_observation_id "
        "        where (x.archived_file_id=af.id or x.source_file_id=af.id) and o.id not in (select id from target_obs)) or "
        " exists(select 1 from condition_ratings x where x.source_file_id=af.id and x.inspection_year_id not in (select id from target_years))"
        ") as deletable from archived_files af join candidate c on c.id=af.id) "
        "select id::text as id, storage_relative_path, deletable from classified order by id",
        plan.bridge_id, plan.inspection_year
    );
    for (const auto& row : files) {
        const auto id = row["id"].as<std::string>();
        plan.fingerprint_items.push_back("file:" + id + ":" + (row["deletable"].as<bool>() ? "delete" : "retain"));
        if (row["deletable"].as<bool>()) {
            plan.archived_file_ids_to_delete.push_back(id);
            plan.archived_file_relative_paths_to_delete.push_back(row["storage_relative_path"].as<std::string>());
        } else {
            ++plan.counts.shared_files_retained;
        }
    }
    plan.counts.archived_files_to_delete = static_cast<int>(plan.archived_file_ids_to_delete.size());
    return plan;
}

void recompute_or_delete_thread(const TransactionPtr& tx, const std::string& thread_id) {
    const auto span = tx->execSqlSync(
        "select "
        "(select o.inspection_year_id::text from defect_observations o join inspection_years iy on iy.id=o.inspection_year_id "
        " where o.defect_thread_id=$1::uuid order by iy.inspection_year asc,iy.is_current desc,iy.version_number desc limit 1) as first_id, "
        "(select o.inspection_year_id::text from defect_observations o join inspection_years iy on iy.id=o.inspection_year_id "
        " where o.defect_thread_id=$1::uuid order by iy.inspection_year desc,iy.is_current desc,iy.version_number desc limit 1) as latest_id",
        thread_id
    );
    if (span.empty() || span[0]["first_id"].isNull()) {
        tx->execSqlSync("delete from defect_threads where id=$1::uuid", thread_id);
        return;
    }
    tx->execSqlSync(
        "update defect_threads set first_seen_inspection_id=$2::uuid, latest_seen_inspection_id=$3::uuid, "
        "updated_at=now() where id=$1::uuid",
        thread_id,
        span[0]["first_id"].as<std::string>(),
        span[0]["latest_id"].as<std::string>()
    );
}

}  // namespace

InspectionYearDeletionRepository::InspectionYearDeletionRepository(drogon::orm::DbClientPtr db_client)
    : db_client_(std::move(db_client)) {}

std::optional<deletion::InspectionYearDeletionPlan> InspectionYearDeletionRepository::preview(
    const std::string& inspection_year_id
) const {
    return build_plan(db_client_, inspection_year_id, false);
}

deletion::DeleteInspectionYearOutcome InspectionYearDeletionRepository::delete_year(
    const std::string& inspection_year_id,
    const std::string& expected_impact_token,
    const std::string& confirmation_text,
    const std::string& reason,
    const deletion::DeletionActorSnapshot& actor
) {
    TransactionPtr tx;
    const auto latch = std::make_shared<CommitLatch>();
    const auto rollback = [&]() {
        if (tx) { try { tx->rollback(); } catch (...) {} }
    };
    try {
        tx = db_client_->newTransaction(latch->callback());
        auto plan = build_plan(tx, inspection_year_id, true);
        if (!plan.has_value()) {
            rollback();
            deletion::DeleteInspectionYearOutcome outcome;
            outcome.status = deletion::DeleteInspectionYearStatus::NotFound;
            return outcome;
        }
        deletion::DeleteInspectionYearOutcome outcome;
        outcome.current_plan = plan;
        if (!plan->active_edit_locks.empty()) {
            rollback();
            outcome.status = deletion::DeleteInspectionYearStatus::Locked;
            return outcome;
        }
        if (plan->impact_token() != expected_impact_token || plan->confirmation_text() != confirmation_text) {
            rollback();
            outcome.status = deletion::DeleteInspectionYearStatus::ImpactChanged;
            return outcome;
        }

        const auto audit_rows = tx->execSqlSync(
            "insert into inspection_year_deletion_audits "
            "(bridge_id, bridge_system_number_snapshot, bridge_name_snapshot, inspection_year, actor_user_id, "
            "actor_username_snapshot, actor_display_name_snapshot, reason, impact_json, deleted_counts_json) "
            "values ($1::uuid,$2,$3,$4,$5::uuid,$6,$7,$8,$9::jsonb,$10::jsonb) returning id::text as id",
            plan->bridge_id, plan->bridge_system_number, plan->bridge_name, plan->inspection_year,
            actor.user_id, actor.username, actor.display_name, reason,
            compact_json(plan->to_public_json()), compact_json(plan->counts.to_json())
        );
        const auto audit_id = audit_rows[0]["id"].as<std::string>();
        for (const auto& path : plan->archived_file_relative_paths_to_delete) {
            tx->execSqlSync(
                "insert into archived_file_deletion_queue(deletion_audit_id,storage_relative_path) values($1::uuid,$2)",
                audit_id, path
            );
        }
        if (plan->archived_file_relative_paths_to_delete.empty()) {
            tx->execSqlSync(
                "update inspection_year_deletion_audits set file_cleanup_status='已完成', "
                "file_cleanup_completed_at=now() where id=$1::uuid",
                audit_id
            );
        }

        tx->execSqlSync(
            "delete from defect_comparisons c using inspection_years cy, inspection_years py "
            "where cy.id=c.current_inspection_year_id and py.id=c.compared_inspection_year_id "
            "and ((cy.bridge_id=$1::uuid and cy.inspection_year=$2) or (py.bridge_id=$1::uuid and py.inspection_year=$2))",
            plan->bridge_id, plan->inspection_year
        );
        tx->execSqlSync(
            "delete from import_records ir using inspection_years iy where iy.id=ir.inspection_year_id "
            "and iy.bridge_id=$1::uuid and iy.inspection_year=$2",
            plan->bridge_id, plan->inspection_year
        );
        tx->execSqlSync(
            "update inspection_years set revision_source_inspection_id=null "
            "where revision_source_inspection_id in "
            "(select id from inspection_years where bridge_id=$1::uuid and inspection_year=$2) "
            "or (bridge_id=$1::uuid and inspection_year=$2)",
            plan->bridge_id, plan->inspection_year
        );
        tx->execSqlSync(
            "delete from inspection_years where bridge_id=$1::uuid and inspection_year=$2",
            plan->bridge_id, plan->inspection_year
        );
        for (const auto& thread_id : plan->defect_thread_ids) recompute_or_delete_thread(tx, thread_id);
        for (const auto& file_id : plan->archived_file_ids_to_delete) {
            tx->execSqlSync("delete from archived_files where id=$1::uuid", file_id);
        }

        const auto next_rows = tx->execSqlSync(
            "select id::text as id from inspection_years where bridge_id=$1::uuid and is_current "
            "order by inspection_year desc, version_number desc limit 1",
            plan->bridge_id
        );
        if (!next_rows.empty()) outcome.next_inspection_year_id = next_rows[0]["id"].as<std::string>();

        tx.reset();
        if (!latch->wait()) {
            outcome.status = deletion::DeleteInspectionYearStatus::Failed;
            return outcome;
        }
        outcome.status = deletion::DeleteInspectionYearStatus::Deleted;
        outcome.deletion_audit_id = audit_id;
        return outcome;
    } catch (const std::exception&) {
        rollback();
        deletion::DeleteInspectionYearOutcome outcome;
        outcome.status = deletion::DeleteInspectionYearStatus::Failed;
        return outcome;
    }
}

}  // namespace bridge_report::db
