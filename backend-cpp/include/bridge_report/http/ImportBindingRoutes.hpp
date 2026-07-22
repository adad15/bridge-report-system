#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "bridge_report/db/ImportBindingRepository.hpp"

namespace bridge_report::http {

// 绑定视图序列化（供前端绑定页与序列化契约测试）。
Json::Value binding_overview_json(const db::BindingOverview& overview);

void register_import_binding_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
