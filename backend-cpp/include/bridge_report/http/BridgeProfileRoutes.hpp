#pragma once

#include <drogon/orm/DbClient.h>

namespace bridge_report::http {

/**
 * @brief 桥梁档案的读写出口（设计 §8 第 1 章）。
 *
 * `GET /api/bridges/{bridge_id}/profile` 读，`PUT` 整体覆盖。报告 §1.1 的叙述文字
 * 和附录2 卡片取的都是这一份数据。
 */
void register_bridge_profile_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
