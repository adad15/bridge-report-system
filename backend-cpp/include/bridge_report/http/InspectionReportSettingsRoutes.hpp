#pragma once

#include <drogon/orm/DbClient.h>

namespace bridge_report::http {

/// 年度报告配置的读写接口（设计 §15.3、§12.1、§21.4）。
///
/// 读会把停用的人员设备和失效的模板照样带出来并标记，让用户看得见历史配置；
/// 写则拒绝停用项，不允许把一个已停用的人静默选进新配置。
/// 历史对比检查的候选条件在列候选和保存时各校验一次。
void register_inspection_report_settings_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
