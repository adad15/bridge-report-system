#include "bridge_report/deletion/ArchivedFileDeletionQueue.hpp"

#include <utility>

#include "bridge_report/deletion/ArchiveFileCleanupCoordinator.hpp"

namespace bridge_report::deletion {

ArchivedFileDeletionQueue::ArchivedFileDeletionQueue(
    drogon::orm::DbClientPtr db_client,
    std::filesystem::path archive_root
) : db_client_(std::move(db_client)), archive_root_(std::move(archive_root)) {}

FileCleanupSummary ArchivedFileDeletionQueue::process_audit(const std::string& deletion_audit_id) const {
    return ArchiveFileCleanupCoordinator(db_client_, archive_root_).process_annual_audit(deletion_audit_id);
}

int ArchivedFileDeletionQueue::process_pending(int audit_limit) const {
    ArchiveFileCleanupPolicy policy;
    policy.batch_size = audit_limit;
    return ArchiveFileCleanupCoordinator(db_client_, archive_root_, policy).process_pending().claimed;
}

}  // namespace bridge_report::deletion
