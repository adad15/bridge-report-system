#pragma once

#include <drogon/orm/DbClient.h>

#include "bridge_report/config/AppConfig.hpp"

namespace bridge_report::http {

/// 报告模板的管理接口（设计 §7、§21.1）。
///
/// 上传流程：接收 multipart -> 落到暂存目录 -> 算 sha256 -> 调 Python 校验 ->
/// 通过才登记进 archived_files 与 report_templates。校验不通过时删掉暂存文件、
/// 不写库，把明细原样返回给管理员（设计 §7.1"校验失败时保留旧模板"）。
///
/// 列表对所有登录用户开放（年度配置页要选模板），但普通用户只看得到已启用的；
/// 其余操作只给管理员（设计 §22）。
void register_report_template_routes(
    const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config);

}  // namespace bridge_report::http
