#pragma once

#include <filesystem>
#include <memory>

#include <drogon/orm/DbClient.h>

namespace bridge_report::deletion {

struct TemporaryWordCleanupPolicy {
    int batch_size{25};
    int parsing_timeout_seconds{900};
    int failed_retention_hours{24};
    int retry_base_seconds{300};
    int retry_max_seconds{86400};
};

struct TemporaryWordCleanupSummary {
    int recovered_parses{0};
    int claimed{0};
    int completed{0};
    int failed{0};
    int orphaned_removed{0};
};

class TemporaryWordCleanupCoordinator {
public:
    TemporaryWordCleanupCoordinator(
        drogon::orm::DbClientPtr db_client,
        std::filesystem::path temporary_word_root,
        TemporaryWordCleanupPolicy policy = {}
    );

    TemporaryWordCleanupSummary process_pending();

private:
    drogon::orm::DbClientPtr db_client_;
    std::filesystem::path temporary_word_root_;
    TemporaryWordCleanupPolicy policy_;
};

}  // namespace bridge_report::deletion
