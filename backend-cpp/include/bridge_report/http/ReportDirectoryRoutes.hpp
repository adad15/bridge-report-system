#pragma once

#include <drogon/orm/DbClient.h>

namespace bridge_report::http {

/// 报告人员库与检测设备库的维护接口（设计 §21.2、§21.3）。
///
/// 读取对所有登录用户开放——年度报告配置页要靠它选人和选设备；
/// 增删改停用只给管理员（设计 §22）。
void register_report_directory_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
