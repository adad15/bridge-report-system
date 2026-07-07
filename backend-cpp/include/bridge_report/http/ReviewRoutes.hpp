#pragma once

#include <drogon/orm/DbClient.h>

namespace bridge_report::http {

/**
 * @brief 注册桥梁导航只读 API：
 *   GET /api/bridges
 *   GET /api/bridges/{bridge_id}/inspection-years
 *   GET /api/bridges/{bridge_id}/import-records
 */
void register_review_routes(const drogon::orm::DbClientPtr& db_client);

}  // 命名空间 bridge_report::http
