#pragma once

#include <filesystem>
#include <string>

#include <drogon/orm/DbClient.h>

#include "bridge_report/deletion/ArchiveFileCleanupCoordinator.hpp"

namespace bridge_report::deletion {

class ArchivedFileDeletionQueue {
public:
    ArchivedFileDeletionQueue(drogon::orm::DbClientPtr db_client, std::filesystem::path archive_root);

    FileCleanupSummary process_audit(const std::string& deletion_audit_id) const;
    int process_pending(int audit_limit = 25) const;

private:
    drogon::orm::DbClientPtr db_client_;
    std::filesystem::path archive_root_;
};

}  // namespace bridge_report::deletion
