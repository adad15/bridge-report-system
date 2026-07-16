#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/db/BridgeAdministrationRepository.hpp"
#include "bridge_report/deletion/ArchiveFileCleanupCoordinator.hpp"

namespace bridge_report::http {

struct DeleteBridgeItemRequest {
    std::string bridge_id;
    std::string impact_token;
};

struct DeleteBridgesRequest {
    std::string reason;
    std::string confirmation_text;
    std::vector<DeleteBridgeItemRequest> items;
};

std::optional<std::string> parse_create_bridge_request(
    const Json::Value& body,
    db::CreateBridgeRequest& output
);
std::optional<std::string> parse_bridge_selection(
    const Json::Value& body,
    std::vector<std::string>& bridge_ids
);
std::optional<std::string> parse_delete_bridges_request(
    const Json::Value& body,
    DeleteBridgesRequest& output
);

void register_bridge_administration_routes(
    const drogon::orm::DbClientPtr& db_client,
    const std::shared_ptr<deletion::ArchiveFileCleanupCoordinator>& cleanup_coordinator
);

}  // namespace bridge_report::http
