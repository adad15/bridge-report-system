#pragma once

#include <drogon/drogon.h>

namespace bridge_report::http {

void apply_local_dev_cors_headers(const drogon::HttpResponsePtr& response);

}  // namespace bridge_report::http
