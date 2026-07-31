#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "bridge_report/review/DefectRatingTreeMatching.hpp"

namespace bridge_report::http {

/// 把批量匹配报告序列化成页面使用的统计 + 逐条结果摘要。
Json::Value defect_match_report_json(const review::DefectMatchReport& report);

/// 注册病害评定树批量匹配路由（页面"重新匹配"与依赖补齐后的复算共用）。
void register_defect_matching_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
