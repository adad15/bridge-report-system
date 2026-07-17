#pragma once

#include <atomic>
#include <filesystem>
#include <string>

#include <drogon/orm/DbClient.h>

namespace bridge_report::deletion {

struct ArchiveFileCleanupPolicy {
    int batch_size{25};
    int claim_timeout_seconds{900};
    int retry_base_seconds{300};
    int retry_max_seconds{86400};
};

struct FileCleanupSummary {
    int claimed{0};
    int completed{0};
    int failed{0};

    FileCleanupSummary& operator+=(const FileCleanupSummary& other) {
        claimed += other.claimed;
        completed += other.completed;
        failed += other.failed;
        return *this;
    }
};

class ArchiveFileCleanupCoordinator {
public:
    enum class QueueKind { Annual, Bridge, Import };

    ArchiveFileCleanupCoordinator(
        drogon::orm::DbClientPtr db_client,
        std::filesystem::path archive_root,
        ArchiveFileCleanupPolicy policy = {}
    );
    ArchiveFileCleanupCoordinator(
        drogon::orm::DbClientPtr db_client,
        std::filesystem::path archive_root,
        std::filesystem::path temporary_word_root,
        ArchiveFileCleanupPolicy policy = {}
    );

    FileCleanupSummary process_pending() const;
    FileCleanupSummary process_annual_audit(const std::string& deletion_audit_id) const;
    FileCleanupSummary process_bridge_audit(const std::string& deletion_audit_id) const;
    FileCleanupSummary process_import_audit(const std::string& deletion_audit_id) const;
    int pending_annual_items(const std::string& deletion_audit_id) const;
    int pending_bridge_items(const std::string& deletion_audit_id) const;
    int pending_import_items(const std::string& deletion_audit_id) const;

private:
    FileCleanupSummary process_kind(QueueKind kind, const std::string* audit_id) const;
    int pending_items(QueueKind kind, const std::string& audit_id) const;

    drogon::orm::DbClientPtr db_client_;
    std::filesystem::path archive_root_;
    std::filesystem::path temporary_word_root_;
    ArchiveFileCleanupPolicy policy_;
    mutable std::atomic_flag pending_run_active_ = ATOMIC_FLAG_INIT;
};

}  // namespace bridge_report::deletion
