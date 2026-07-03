#pragma once

#include <drogon/HttpResponse.h>

namespace bridge_report::http {

/**
 * @brief 添加本地开发环境所需的 CORS 响应头。
 */
void apply_local_dev_cors_headers(const drogon::HttpResponsePtr& response);

}  // 命名空间 bridge_report::http
