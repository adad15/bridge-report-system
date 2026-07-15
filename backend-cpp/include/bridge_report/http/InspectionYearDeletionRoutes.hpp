#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

namespace bridge_report::http {

struct DeleteInspectionYearRequest {
    std::string impact_token;
    std::string confirmation_text;
    std::string reason;
};

[[nodiscard]] std::optional<std::string> parse_delete_inspection_year_request(
    const Json::Value& body,
    DeleteInspectionYearRequest& out
);

void register_inspection_year_deletion_routes(
    const drogon::orm::DbClientPtr& db_client,
    const std::filesystem::path& archive_root
);

}  // namespace bridge_report::http
