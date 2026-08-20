#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <string>

#include "bridge_report/db/ImportBindingRepository.hpp"

namespace bridge_report::http {

// 绑定视图序列化（供前端绑定页与序列化契约测试）。
Json::Value binding_overview_json(const db::BindingOverview& overview);

// 仓储结果 → 错误码 + 提示 + HTTP 状态。
//
// 抽出来是为了能测：本仓库没有 HTTP 级夹具，把这段决策埋在 lambda 里就没人验得了
// "哪个码配哪个状态"，而那正是最容易出错的地方（Invalid 分支曾经把仓储带回的
// 具体错误码整个丢掉，一律报 invalid_component_binding）。
struct BindingErrorResponse {
    std::string error_code;
    std::string error_message;
    int http_status{200};
};

[[nodiscard]] BindingErrorResponse binding_error_response(const db::BindingOutcome& outcome);

void register_import_binding_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
