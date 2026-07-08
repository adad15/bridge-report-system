#pragma once

#include <drogon/orm/DbClient.h>

namespace bridge_report::http {

/**
 * @brief 注册桥梁导航与校对工作台 API：
 *   GET /api/bridges
 *   GET /api/bridges/{bridge_id}/inspection-years
 *   GET /api/bridges/{bridge_id}/import-records
 *   GET /api/import-records/{import_record_id}/review
 *   PUT /api/import-records/{import_record_id}/review-draft
 *   POST /api/import-records/{import_record_id}/cancel
 */
void register_review_routes(const drogon::orm::DbClientPtr& db_client);

}  // 命名空间 bridge_report::http
