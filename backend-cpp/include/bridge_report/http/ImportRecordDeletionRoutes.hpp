#pragma once

#include <memory>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/deletion/ArchiveFileCleanupCoordinator.hpp"

namespace bridge_report::http {

struct DeleteImportRecordRequest {
    std::string impact_token;
    std::string confirmation_text;
    std::string reason;
};

[[nodiscard]] std::optional<std::string> parse_delete_import_record_request(
    const Json::Value& body,
    DeleteImportRecordRequest& out
);

void register_import_record_deletion_routes(
    const drogon::orm::DbClientPtr& db_client,
    const std::shared_ptr<deletion::ArchiveFileCleanupCoordinator>& cleanup_coordinator
);

}  // namespace bridge_report::http
