#include <functional>
#include <iostream>
#include <memory>
#include <string>

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Exception.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/AuthRepository.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/ComponentArchiveRoutes.hpp"
#include "bridge_report/http/Cors.hpp"
#include "bridge_report/http/DefectThreadRoutes.hpp"
#include "bridge_report/http/EditLockRoutes.hpp"
#include "bridge_report/http/ImportConfirmRoutes.hpp"
#include "bridge_report/http/InspectionYearDeletionRoutes.hpp"
#include "bridge_report/http/ReviewRoutes.hpp"
#include "bridge_report/http/WordImportRoutes.hpp"
#include "bridge_report/http/WorkspaceRoutes.hpp"
#include "bridge_report/runtime/RuntimePaths.hpp"
#include "bridge_report/deletion/ArchivedFileDeletionQueue.hpp"

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

void register_health_routes(
    const bridge_report::config::AppConfig& config,
    const drogon::orm::DbClientPtr& db_client
) {
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
    register_options_handler("/health/db");

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

                    // 这里只说明已收到 Python 工具服务响应；是否健康由上游状态码和响应体表达。
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

    // 注意：execSqlSync 会阻塞当前 IO 线程；本地单用户 v1 场景可接受。
    drogon::app().registerHandler(
        "/health/db",
        [db_client](
            const drogon::HttpRequestPtr&,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback
        ) {
            const auto respond_degraded = [&callback]() {
                Json::Value body;
                body["status"] = "degraded";
                body["database"] = "unavailable";
                auto response = drogon::HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(drogon::k503ServiceUnavailable);
                bridge_report::http::apply_local_dev_cors_headers(response);
                callback(response);
            };

            try {
                db_client->execSqlSync("select 1");

                Json::Value body;
                body["status"] = "ok";
                body["database"] = "reachable";
                auto response = drogon::HttpResponse::newHttpJsonResponse(body);
                bridge_report::http::apply_local_dev_cors_headers(response);
                callback(response);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_degraded();
            } catch (const std::exception&) {
                // 兜底：健康检查处理器内不允许任何异常向外逃逸。
                respond_degraded();
            }
        },
        {drogon::Get}
    );
}

}  // 匿名命名空间

int main(int argc, char* argv[]) {
    const std::string config_path = argc > 1 ? argv[1] : "config/local.json";
    const auto config = bridge_report::config::load_app_config(config_path);

    const auto db_client = bridge_report::db::create_db_client(config.postgres);

    // 默认账号播种：users 表为空时预置 admin/admin123 与 user/user123。
    // 数据库暂不可用时不阻断启动（/health/db 会如实报告），下次重启再播种。
    try {
        bridge_report::db::AuthRepository auth_repository(db_client);
        if (auth_repository.seed_default_users() > 0) {
            std::cout << "已播种默认账号 admin(管理员) / user(普通)，初始密码见 docs，请尽快修改。\n";
        }
    } catch (const std::exception& error) {
        std::cout << "默认账号播种失败（稍后可重启重试）：" << error.what() << "\n";
    }

    // 上次删除若因进程异常未完成物理文件清理，启动时做一次有界重试。
    try {
        bridge_report::deletion::ArchivedFileDeletionQueue queue(db_client, config.archive_root);
        queue.process_pending();
    } catch (const std::exception& error) {
        std::cout << "归档文件待清理队列重试失败：" << error.what() << "\n";
    }

    drogon::app().registerMiddleware(std::make_shared<drogon::HttpOptionsMiddleware>());
    register_health_routes(config, db_client);
    bridge_report::http::register_auth_routes(db_client);
    bridge_report::http::register_edit_lock_routes(db_client);
    bridge_report::http::register_review_routes(db_client, config.archive_root);
    bridge_report::http::register_import_confirm_routes(db_client);
    bridge_report::http::register_word_import_routes(db_client, config);
    bridge_report::http::register_workspace_routes(db_client, config);
    bridge_report::http::register_component_archive_routes(db_client, config.archive_root);
    bridge_report::http::register_defect_thread_routes(db_client);
    bridge_report::http::register_inspection_year_deletion_routes(db_client, config.archive_root);

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
