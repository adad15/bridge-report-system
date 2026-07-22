#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

namespace bridge_report::db {

struct CreateBridgeRequest {
    std::string bridge_name;
    std::optional<std::string> route_number;
    std::optional<std::string> route_name;
    std::optional<std::string> administrative_region;
    std::optional<std::string> station_mark;
    std::string status{"在用"};
    std::optional<std::string> bridge_scale;  // 大桥/中桥/小桥，用户手工录入
};

struct BridgeAdministrationSummary {
    std::string id;
    std::string system_number;
    std::string bridge_name;
    std::optional<std::string> route_number;
    std::optional<std::string> route_name;
    std::optional<std::string> administrative_region;
    std::optional<std::string> station_mark;
    std::string status;
    std::optional<std::string> bridge_scale;
};

enum class CreateBridgeStatus { Created, Duplicate, Invalid, Failed };

struct CreateBridgeOutcome {
    CreateBridgeStatus status{CreateBridgeStatus::Failed};
    std::optional<BridgeAdministrationSummary> bridge;
};

class BridgeAdministrationRepository {
public:
    explicit BridgeAdministrationRepository(drogon::orm::DbClientPtr db_client);
    CreateBridgeOutcome create_bridge(const CreateBridgeRequest& request);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
