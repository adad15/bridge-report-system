#pragma once

#include <memory>

#include <drogon/orm/DbClient.h>

#include "bridge_report/report/ReportGenerationRunner.hpp"

namespace bridge_report::http {

/**
 * @brief 报告生成任务的 HTTP 出口（设计 §17、§21.4、§22）。
 *
 * 三个入口：创建任务、查任务、下载成品。任务本身由 runner 在自己的线程里跑，
 * 创建接口立刻返回 queued，前端轮询状态。
 */
void register_report_generation_routes(
    const drogon::orm::DbClientPtr& db_client,
    std::shared_ptr<report::ReportGenerationRunner> runner);

}  // namespace bridge_report::http
