#pragma once

#include <memory>

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "bridge_report/standards/StandardRegistry.hpp"

namespace bridge_report::http {

bool parse_standard_enabled_request(const Json::Value& body, bool& enabled);

void register_standard_routes(
    const drogon::orm::DbClientPtr& db_client,
    std::shared_ptr<const standards::StandardRegistry> registry);

}  // namespace bridge_report::http
