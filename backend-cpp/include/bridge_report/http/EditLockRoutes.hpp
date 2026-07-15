#pragma once

#include <string>

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/db/AuthRepository.hpp"
#include "bridge_report/db/EditLockRepository.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {

std::string edit_lock_token_from_request(const drogon::HttpRequestPtr& request);
Json::Value edit_lock_info_to_json(const db::EditLockInfo& lock, const std::string& current_user_id = std::string());

bool require_active_edit_lock(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const std::string& import_record_id,
    const db::AuthUser& user,
    const HttpCallback& callback
);

void register_edit_lock_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
