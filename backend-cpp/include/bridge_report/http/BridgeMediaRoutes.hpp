#pragma once

#include <drogon/orm/DbClient.h>

#include "bridge_report/config/AppConfig.hpp"

namespace bridge_report::http {

/**
 * @brief 桥梁图件的上传、删除、列表与内容。
 *
 * 图件属于桥本身，不属于哪一年：地理位置图、桥型布置图、横断面图和三张桥梁照片。
 * 报告 §1.1 从这里取图。
 */
void register_bridge_media_routes(
    const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config);

}  // namespace bridge_report::http
