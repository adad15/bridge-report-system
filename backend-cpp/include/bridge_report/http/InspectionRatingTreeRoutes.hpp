#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <string>

#include "bridge_report/db/InspectionRatingTreeRepository.hpp"

namespace bridge_report::http {

// 仓储结果 → 错误码 + 提示 + HTTP 状态。
//
// 抽出来是为了能测：本仓库没有 HTTP 级夹具，把这段决策埋在 lambda 里就没人验得了
// "哪个码配哪个状态"，而那正是最容易出错的地方（Invalid 分支曾经把仓储带回的
// 具体错误码整个丢掉，一律报 invalid_component_binding）。
struct RatingTreeBindingErrorResponse {
    std::string error_code;
    std::string error_message;
    int http_status{200};
};

[[nodiscard]] RatingTreeBindingErrorResponse rating_tree_binding_error_response(
    const db::RatingTreeBindingOutcome& outcome);

void register_inspection_rating_tree_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
