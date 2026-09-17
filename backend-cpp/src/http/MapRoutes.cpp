#include "bridge_report/http/MapRoutes.hpp"

#include <string>

#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {

void register_map_routes(
    const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config) {
    const std::string path = "/api/config/map";
    register_options_handler(path);

    // 只给登录用户：key 虽然受域名白名单保护，也没必要对匿名请求敞开。
    drogon::app().registerHandler(
        path,
        [db_client, js_key = config.map.js_key,
         security_js_code = config.map.security_js_code](
            const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback);
                    return;
                }
                Json::Value body;
                body["js_key"] = js_key;
                body["security_js_code"] = security_js_code;
                // 没配 key 就没有地图，前端据此换成已经存下的地理位置图，而不是摆一个空框。
                body["available"] = !js_key.empty();
                respond_json(callback, body);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});
}

}  // namespace bridge_report::http
