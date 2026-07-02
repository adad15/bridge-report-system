#include "bridge_report/http/Cors.hpp"

namespace bridge_report::http {

void apply_local_dev_cors_headers(const drogon::HttpResponsePtr& response) {
    response->addHeader("Access-Control-Allow-Origin", "http://127.0.0.1:5173");
    response->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

}  // namespace bridge_report::http
