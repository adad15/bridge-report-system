#include <functional>
#include <iostream>
#include <memory>
#include <string>

#include <drogon/drogon.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/http/Cors.hpp"
#include "bridge_report/runtime/RuntimePaths.hpp"

namespace {

Json::Value make_cpp_health_body(const bridge_report::config::AppConfig& config) {
    Json::Value body;
    body["status"] = "ok";
    body["service"] = "bridge-report-cpp-backend";
    body["version"] = "0.1.0";
    body["host"] = config.host;
    body["port"] = config.port;
    body["python_tools_base_url"] = config.python_tools_base_url;
    body["archive_root"] = config.archive_root.generic_string();
    return body;
}

void register_health_routes(const bridge_report::config::AppConfig& config) {
    const auto register_options_handler = [](const std::string& path) {
        drogon::app().registerHandler(
            path,
            [](const drogon::HttpRequestPtr&,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
                auto response = drogon::HttpResponse::newHttpResponse();
                bridge_report::http::apply_local_dev_cors_headers(response);
                callback(response);
            },
            {drogon::Options, "drogon::HttpOptionsMiddleware"}
        );
    };

    register_options_handler("/health");
    register_options_handler("/health/tools");

    drogon::app().registerHandler(
        "/health",
        [config](const drogon::HttpRequestPtr&,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto response = drogon::HttpResponse::newHttpJsonResponse(make_cpp_health_body(config));
            bridge_report::http::apply_local_dev_cors_headers(response);
            callback(response);
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/health/tools",
        [base_url = config.python_tools_base_url](
            const drogon::HttpRequestPtr&,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback
        ) {
            auto client = drogon::HttpClient::newHttpClient(base_url);
            auto request = drogon::HttpRequest::newHttpRequest();
            request->setMethod(drogon::Get);
            request->setPath("/health");

            client->sendRequest(
                request,
                [callback = std::move(callback)](
                    drogon::ReqResult result,
                    const drogon::HttpResponsePtr& tools_response
                ) mutable {
                    Json::Value body;
                    body["service"] = "bridge-report-cpp-backend";
                    body["checked_service"] = "bridge-report-python-tools";

                    if (result != drogon::ReqResult::Ok || tools_response == nullptr) {
                        body["status"] = "degraded";
                        body["tools_status"] = "unavailable";
                        auto response = drogon::HttpResponse::newHttpJsonResponse(body);
                        response->setStatusCode(drogon::k503ServiceUnavailable);
                        bridge_report::http::apply_local_dev_cors_headers(response);
                        callback(response);
                        return;
                    }

                    body["status"] = "ok";
                    body["tools_status"] = "reachable";
                    body["tools_http_status"] = static_cast<int>(tools_response->statusCode());
                    body["tools_response_raw"] = std::string(tools_response->body());
                    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
                    bridge_report::http::apply_local_dev_cors_headers(response);
                    callback(response);
                }
            );
        },
        {drogon::Get}
    );
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string config_path = argc > 1 ? argv[1] : "config/local.json";
    const auto config = bridge_report::config::load_app_config(config_path);

    drogon::app().registerMiddleware(std::make_shared<drogon::HttpOptionsMiddleware>());

    register_health_routes(config);

    std::cout << "Bridge Report C++ backend listening on "
              << config.host << ":" << config.port << "\n";

    const std::string log_path = "logs";
    bridge_report::runtime::ensure_log_directory(log_path);

    drogon::app()
        .addListener(config.host, config.port)
        .setLogPath(log_path)
        .setLogLevel(trantor::Logger::kInfo)
        .run();

    return 0;
}
