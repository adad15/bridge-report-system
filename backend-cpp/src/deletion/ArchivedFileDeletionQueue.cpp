#include "bridge_report/deletion/ArchivedFileDeletionQueue.hpp"

#include <system_error>
#include <utility>

#include "bridge_report/archive/ArchivePaths.hpp"

namespace bridge_report::deletion {

ArchivedFileDeletionQueue::ArchivedFileDeletionQueue(
    drogon::orm::DbClientPtr db_client,
    std::filesystem::path archive_root
) : db_client_(std::move(db_client)), archive_root_(std::move(archive_root)) {}

FileCleanupSummary ArchivedFileDeletionQueue::process_audit(const std::string& deletion_audit_id) const {
    FileCleanupSummary summary;
    const auto rows = db_client_->execSqlSync(
        "select id::text as id, storage_relative_path from archived_file_deletion_queue "
        "where deletion_audit_id=$1::uuid and status in ('待清理','失败待重试') order by created_at",
        deletion_audit_id
    );
    for (const auto& row : rows) {
        const auto queue_id = row["id"].as<std::string>();
        try {
            const auto resolved = archive::resolve_path_under_root(
                archive_root_, std::filesystem::path(row["storage_relative_path"].as<std::string>()));
            std::error_code error;
            std::filesystem::remove(resolved, error);
            if (error) throw std::system_error(error);
            db_client_->execSqlSync(
                "update archived_file_deletion_queue set status='已完成', attempt_count=attempt_count+1, "
                "last_error=null, completed_at=now() where id=$1::uuid",
                queue_id
            );
            ++summary.completed;
        } catch (const std::exception& error) {
            db_client_->execSqlSync(
                "update archived_file_deletion_queue set status='失败待重试', attempt_count=attempt_count+1, "
                "last_error=$2, completed_at=null where id=$1::uuid",
                queue_id, std::string(error.what()).substr(0, 2000)
            );
            ++summary.failed;
        }
    }

    const auto remaining = db_client_->execSqlSync(
        "select count(*) as count from archived_file_deletion_queue "
        "where deletion_audit_id=$1::uuid and status<>'已完成'",
        deletion_audit_id
    )[0]["count"].as<int>();
    db_client_->execSqlSync(
        "update inspection_year_deletion_audits set file_cleanup_status=$2, "
        "file_cleanup_completed_at=case when $2='已完成' then now() else null end where id=$1::uuid",
        deletion_audit_id, remaining == 0 ? "已完成" : "部分失败"
    );
    return summary;
}

int ArchivedFileDeletionQueue::process_pending(int audit_limit) const {
    const auto rows = db_client_->execSqlSync(
        "select distinct q.deletion_audit_id::text as id from archived_file_deletion_queue q "
        "where q.status in ('待清理','失败待重试') order by q.deletion_audit_id limit $1",
        audit_limit
    );
    for (const auto& row : rows) process_audit(row["id"].as<std::string>());
    return static_cast<int>(rows.size());
}

}  // namespace bridge_report::deletion
