#include "bridge_report/deletion/ArchiveFileCleanupCoordinator.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "bridge_report/deletion/ArchiveFileDeletionCore.hpp"

namespace bridge_report::deletion {
namespace {

struct QueueMetadata {
    const char* queue_table;
    const char* audit_column;
    const char* audit_table;
};

QueueMetadata metadata(const bool bridge) {
    return bridge
        ? QueueMetadata{"bridge_archived_file_deletion_queue", "bridge_deletion_audit_id",
                        "bridge_deletion_audits"}
        : QueueMetadata{"archived_file_deletion_queue", "deletion_audit_id",
                        "inspection_year_deletion_audits"};
}

struct ClaimedItem {
    std::string id;
    std::string audit_id;
    std::string storage_relative_path;
    std::string processing_started_at;
    int attempt_count{0};
};

int normalized_positive(const int value, const int fallback) {
    return value > 0 ? value : fallback;
}

std::string truncate_error(const std::exception& error) {
    auto message = std::string(error.what());
    if (message.size() > 2000) message.resize(2000);
    return message;
}

}  // namespace

ArchiveFileCleanupCoordinator::ArchiveFileCleanupCoordinator(
    drogon::orm::DbClientPtr db_client,
    std::filesystem::path archive_root,
    ArchiveFileCleanupPolicy policy
) : db_client_(std::move(db_client)), archive_root_(std::move(archive_root)), policy_(policy) {
    policy_.batch_size = normalized_positive(policy_.batch_size, 25);
    policy_.claim_timeout_seconds = normalized_positive(policy_.claim_timeout_seconds, 900);
    policy_.retry_base_seconds = normalized_positive(policy_.retry_base_seconds, 300);
    policy_.retry_max_seconds = (std::max)(
        policy_.retry_base_seconds, normalized_positive(policy_.retry_max_seconds, 86400));
}

FileCleanupSummary ArchiveFileCleanupCoordinator::process_pending() const {
    if (pending_run_active_.test_and_set()) return {};
    struct ClearFlag {
        std::atomic_flag& flag;
        ~ClearFlag() { flag.clear(); }
    } clear{pending_run_active_};
    FileCleanupSummary result;
    result += process_kind(QueueKind::Annual, nullptr);
    result += process_kind(QueueKind::Bridge, nullptr);
    return result;
}

FileCleanupSummary ArchiveFileCleanupCoordinator::process_annual_audit(
    const std::string& deletion_audit_id
) const {
    return process_kind(QueueKind::Annual, &deletion_audit_id);
}

FileCleanupSummary ArchiveFileCleanupCoordinator::process_bridge_audit(
    const std::string& deletion_audit_id
) const {
    return process_kind(QueueKind::Bridge, &deletion_audit_id);
}

FileCleanupSummary ArchiveFileCleanupCoordinator::process_kind(
    const QueueKind kind,
    const std::string* audit_id
) const {
    const auto spec = metadata(kind == QueueKind::Bridge);
    std::string sql =
        "with candidates as (select id from " + std::string(spec.queue_table) +
        " where ((status in ('待清理','失败待重试') and next_attempt_at<=now()) "
        "or (status='清理中' and processing_started_at<=now()-make_interval(secs=>$1::int))) ";
    if (audit_id != nullptr) sql += "and " + std::string(spec.audit_column) + "=$2::uuid ";
    sql += "order by coalesce(next_attempt_at,created_at),created_at for update skip locked limit $";
    sql += audit_id == nullptr ? "2::int" : "3::int";
    sql += ") update " + std::string(spec.queue_table) +
        " q set status='清理中',processing_started_at=clock_timestamp(),completed_at=null "
        "from candidates c where q.id=c.id returning q.id::text as id,q." +
        std::string(spec.audit_column) +
        "::text as audit_id,q.storage_relative_path,q.processing_started_at::text as claimed_at,"
        "q.attempt_count";

    drogon::orm::Result rows;
    if (audit_id == nullptr) {
        rows = db_client_->execSqlSync(sql, policy_.claim_timeout_seconds, policy_.batch_size);
    } else {
        rows = db_client_->execSqlSync(
            sql, policy_.claim_timeout_seconds, *audit_id, policy_.batch_size);
    }

    std::vector<ClaimedItem> items;
    items.reserve(rows.size());
    for (const auto& row : rows) {
        items.push_back({
            row["id"].as<std::string>(),
            row["audit_id"].as<std::string>(),
            row["storage_relative_path"].as<std::string>(),
            row["claimed_at"].as<std::string>(),
            row["attempt_count"].as<int>()
        });
    }

    FileCleanupSummary summary;
    summary.claimed = static_cast<int>(items.size());
    ArchiveFileDeletionCore core(archive_root_);
    std::set<std::string> affected_audits;
    for (const auto& item : items) {
        affected_audits.insert(item.audit_id);
        try {
            core.remove(item.storage_relative_path);
            const auto updated = db_client_->execSqlSync(
                "update " + std::string(spec.queue_table) +
                " set status='已完成',attempt_count=attempt_count+1,last_error=null,"
                "processing_started_at=null,completed_at=now() "
                "where id=$1::uuid and status='清理中' and processing_started_at=$2::timestamptz",
                item.id, item.processing_started_at
            );
            if (updated.affectedRows() > 0) ++summary.completed;
        } catch (const std::exception& error) {
            std::int64_t delay = policy_.retry_base_seconds;
            for (int i = 0; i < item.attempt_count && delay < policy_.retry_max_seconds; ++i) {
                delay = (std::min<std::int64_t>)(delay * 2, policy_.retry_max_seconds);
            }
            const auto updated = db_client_->execSqlSync(
                "update " + std::string(spec.queue_table) +
                " set status='失败待重试',attempt_count=attempt_count+1,last_error=$3,"
                "next_attempt_at=now()+make_interval(secs=>$4::int),processing_started_at=null,completed_at=null "
                "where id=$1::uuid and status='清理中' and processing_started_at=$2::timestamptz",
                item.id, item.processing_started_at, truncate_error(error), static_cast<int>(delay)
            );
            if (updated.affectedRows() > 0) ++summary.failed;
        }
    }

    for (const auto& affected_audit : affected_audits) {
        db_client_->execSqlSync(
            "update " + std::string(spec.audit_table) + " a set "
            "file_cleanup_status=case "
            "when not exists (select 1 from " + std::string(spec.queue_table) + " q where q." +
            std::string(spec.audit_column) + "=a.id and q.status<>'已完成') then '已完成' "
            "when exists (select 1 from " + std::string(spec.queue_table) + " q where q." +
            std::string(spec.audit_column) + "=a.id and q.status='失败待重试') then '部分失败' "
            "else '待清理' end,"
            "file_cleanup_completed_at=case when not exists (select 1 from " +
            std::string(spec.queue_table) + " q where q." + std::string(spec.audit_column) +
            "=a.id and q.status<>'已完成') then now() else null end where a.id=$1::uuid",
            affected_audit
        );
    }
    return summary;
}

int ArchiveFileCleanupCoordinator::pending_items(
    const QueueKind kind,
    const std::string& audit_id
) const {
    const auto spec = metadata(kind == QueueKind::Bridge);
    return db_client_->execSqlSync(
        "select count(*) as count from " + std::string(spec.queue_table) + " where " +
        std::string(spec.audit_column) + "=$1::uuid and status<>'已完成'", audit_id
    )[0]["count"].as<int>();
}

int ArchiveFileCleanupCoordinator::pending_annual_items(const std::string& deletion_audit_id) const {
    return pending_items(QueueKind::Annual, deletion_audit_id);
}

int ArchiveFileCleanupCoordinator::pending_bridge_items(const std::string& deletion_audit_id) const {
    return pending_items(QueueKind::Bridge, deletion_audit_id);
}

}  // namespace bridge_report::deletion
