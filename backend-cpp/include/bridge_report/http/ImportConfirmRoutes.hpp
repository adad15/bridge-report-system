#pragma once

#include <drogon/orm/DbClient.h>

namespace bridge_report::http {

/**
 * @brief 注册"入库前校验 / 正式入库"相关路由（同一"写前校验"关注点，故放在一起）：
 *   POST /api/import-records/{import_record_id}/preflight-confirm
 *   POST /api/import-records/{import_record_id}/confirm
 */
void register_import_confirm_routes(const drogon::orm::DbClientPtr& db_client);

}  // 命名空间 bridge_report::http
