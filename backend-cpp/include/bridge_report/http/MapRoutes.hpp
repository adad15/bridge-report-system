#pragma once

#include <drogon/orm/DbClient.h>

#include "bridge_report/config/AppConfig.hpp"

namespace bridge_report::http {

/**
 * @brief 地图服务的前端配置。
 *
 * 只下发浏览器用的 Web 端 key。服务端调静态地图接口的那个 key 留在后端，永不出现在
 * 任何响应里。key 没配就返回空串，前端据此退回显示已经存下的地理位置图。
 */
void register_map_routes(
    const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config);

}  // namespace bridge_report::http
