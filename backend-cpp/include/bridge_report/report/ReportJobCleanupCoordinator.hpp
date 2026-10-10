#pragma once
#include <filesystem>
#include <drogon/orm/DbClient.h>
#include "bridge_report/db/ReportGenerationJobRepository.hpp"

namespace bridge_report::report {
class ReportJobCleanupCoordinator {
public:
    ReportJobCleanupCoordinator(drogon::orm::DbClientPtr client, std::filesystem::path root);
    db::JobCleanupSummary run(int history_days) const;
private:
    drogon::orm::DbClientPtr client_;
    std::filesystem::path root_;
};
}
