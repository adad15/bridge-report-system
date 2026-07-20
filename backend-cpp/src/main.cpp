#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Exception.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/AuthRepository.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/StandardRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/AssessmentRoutes.hpp"
#include "bridge_report/http/BridgeAdministrationRoutes.hpp"
#include "bridge_report/http/ComponentArchiveRoutes.hpp"
#include "bridge_report/http/ComponentInventoryRoutes.hpp"
#include "bridge_report/http/Cors.hpp"
#include "bridge_report/http/DefectThreadRoutes.hpp"
#include "bridge_report/http/EditLockRoutes.hpp"
#include "bridge_report/http/ImportConfirmRoutes.hpp"
#include "bridge_report/http/ImportRecordDeletionRoutes.hpp"
#include "bridge_report/http/InspectionYearDeletionRoutes.hpp"
#include "bridge_report/http/ReviewRoutes.hpp"
#include "bridge_report/http/StandardRoutes.hpp"
#include "bridge_report/http/WordImportRoutes.hpp"
#include "bridge_report/http/WorkspaceRoutes.hpp"
#include "bridge_report/runtime/RuntimePaths.hpp"
#include "bridge_report/standards/StandardPackageLoader.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"
#include "bridge_report/deletion/ArchiveFileCleanupCoordinator.hpp"
#include "bridge_report/deletion/TemporaryWordCleanupCoordinator.hpp"

namespace {

struct StandardStartupState {
    std::shared_ptr<bridge_report::standards::StandardRegistry> registry;
    std::vector<bridge_report::standards::StandardManifest> manifests;
    std::vector<bridge_report::standards::StandardIssue> issues;
};

StandardStartupState load_standard_registry(const std::filesystem::path& standards_root) {
    bridge_report::standards::StandardPackageLoader loader;
    StandardStartupState state;
    state.registry = std::make_shared<bridge_report::standards::StandardRegistry>();

    std::error_code root_error;
    if (!std::filesystem::is_directory(standards_root, root_error) || root_error) {
        bridge_report::standards::StandardIssue root_issue{
            "standards_root_unavailable",
            "规范包根目录不存在或不可读取。",
        };
        std::cerr << "规范包加载失败 [" << root_issue.code << "]："
                  << root_issue.message << "\n";
        state.issues.push_back(std::move(root_issue));
        return state;
    }

    for (const auto& package_root : loader.discover(standards_root)) {
        auto load_result = loader.load(package_root);
        if (!load_result.ok()) {
            for (auto& load_issue : load_result.issues) {
                std::cerr << "规范包加载失败 [" << load_issue.code << "]："
                          << load_issue.message << "\n";
                state.issues.push_back(std::move(load_issue));
            }
            continue;
        }

        const auto manifest = load_result.package->manifest;
        auto registration = state.registry->register_package(std::move(*load_result.package));
        if (!registration.accepted && registration.issue.has_value()) {
            std::cerr << "规范包注册失败 [" << registration.issue->code << "]："
                      << registration.issue->message << "\n";
            state.issues.push_back(std::move(*registration.issue));
        } else if (registration.accepted) {
            state.manifests.push_back(manifest);
        }
    }
    return state;
}

Json::Value make_cpp_health_body(
    const bridge_report::config::AppConfig& config,
    const std::size_t standard_package_count,
    const std::size_t standard_error_count) {
    Json::Value body;
    body["status"] = "ok";
    body["service"] = "bridge-report-cpp-backend";
    body["version"] = "0.1.0";
    body["host"] = config.host;
    body["port"] = config.port;
    body["python_tools_base_url"] = config.python_tools_base_url;
    body["archive_root"] = config.archive_root.generic_string();
    body["standards_status"] = standard_error_count == 0 ? "ok" : "degraded";
    body["standard_package_count"] = static_cast<Json::UInt64>(standard_package_count);
    body["standard_error_count"] = static_cast<Json::UInt64>(standard_error_count);
    return body;
}

void register_health_routes(
    const bridge_report::config::AppConfig& config,
    const drogon::orm::DbClientPtr& db_client,
    const std::size_t standard_package_count,
    const std::size_t standard_error_count
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
        [config, standard_package_count, standard_error_count](const drogon::HttpRequestPtr&,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto response = drogon::HttpResponse::newHttpJsonResponse(
                make_cpp_health_body(config, standard_package_count, standard_error_count));
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
    auto standards = load_standard_registry(config.standards_root);

    const auto db_client = bridge_report::db::create_db_client(config.postgres);

    // 规则包文件是运行时真源；数据库只同步不可变身份与项目引用所需元数据。
    // 同身份同版本摘要冲突或数据库暂不可用时不覆盖旧记录，也不阻断 HTTP 启动。
    try {
        bridge_report::db::StandardRepository standard_repository(db_client);
        const auto outcomes = standard_repository.sync_packages(standards.manifests);
        for (std::size_t index = 0; index < outcomes.size(); ++index) {
            if (outcomes[index].status !=
                bridge_report::db::StandardPackageSyncStatus::ChecksumConflict) {
                continue;
            }
            bridge_report::standards::StandardIssue conflict{
                "package_database_checksum_conflict",
                "数据库中同一规范身份和包版本已对应其他内容摘要。",
            };
            std::cerr << "规范包数据库同步失败 [" << conflict.code << "]："
                      << standards.manifests[index].standard_code << " "
                      << standards.manifests[index].package_version << "；"
                      << conflict.message << "\n";
            standards.issues.push_back(std::move(conflict));
        }
    } catch (const std::exception& error) {
        bridge_report::standards::StandardIssue sync_issue{
            "package_database_sync_failed",
            "规范包元数据暂时无法同步到数据库。",
        };
        std::cerr << "规范包数据库同步失败 [" << sync_issue.code << "]："
                  << error.what() << "\n";
        standards.issues.push_back(std::move(sync_issue));
    }

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

    bridge_report::deletion::ArchiveFileCleanupPolicy cleanup_policy;
    cleanup_policy.batch_size = config.cleanup_batch_size;
    cleanup_policy.claim_timeout_seconds = config.cleanup_claim_timeout_seconds;
    cleanup_policy.retry_base_seconds = config.cleanup_retry_base_seconds;
    cleanup_policy.retry_max_seconds = config.cleanup_retry_max_seconds;
    const auto cleanup_coordinator =
        std::make_shared<bridge_report::deletion::ArchiveFileCleanupCoordinator>(
            db_client, config.archive_root, config.temporary_word_root, cleanup_policy);

    bridge_report::deletion::TemporaryWordCleanupPolicy temporary_cleanup_policy;
    temporary_cleanup_policy.batch_size = config.cleanup_batch_size;
    temporary_cleanup_policy.parsing_timeout_seconds = config.cleanup_claim_timeout_seconds;
    temporary_cleanup_policy.failed_retention_hours = config.failed_word_retention_hours;
    temporary_cleanup_policy.retry_base_seconds = config.cleanup_retry_base_seconds;
    temporary_cleanup_policy.retry_max_seconds = config.cleanup_retry_max_seconds;
    const auto temporary_word_cleanup =
        std::make_shared<bridge_report::deletion::TemporaryWordCleanupCoordinator>(
            db_client, config.temporary_word_root, temporary_cleanup_policy);

    // 启动时处理遗留项，并在运行期间持续有界重试。异常不得阻断 HTTP 服务。
    drogon::app().registerBeginningAdvice(
        [cleanup_coordinator, temporary_word_cleanup, interval = config.cleanup_interval_seconds]() {
            try {
                cleanup_coordinator->process_pending();
                temporary_word_cleanup->process_pending();
            } catch (const std::exception& error) {
                std::cout << "归档文件启动清理失败：" << error.what() << "\n";
            }
            drogon::app().getLoop()->runEvery(
                static_cast<double>(interval),
                [cleanup_coordinator, temporary_word_cleanup]() {
                    try {
                        cleanup_coordinator->process_pending();
                        temporary_word_cleanup->process_pending();
                    } catch (const std::exception& error) {
                        std::cout << "归档文件定时清理失败：" << error.what() << "\n";
                    }
                }
            );
        }
    );

    drogon::app().registerMiddleware(std::make_shared<drogon::HttpOptionsMiddleware>());
    register_health_routes(
        config,
        db_client,
        standards.registry->package_count(),
        standards.issues.size());
    bridge_report::http::register_auth_routes(db_client);
    bridge_report::http::register_edit_lock_routes(db_client);
    bridge_report::http::register_review_routes(db_client, config.archive_root);
    bridge_report::http::register_bridge_administration_routes(db_client, cleanup_coordinator);
    bridge_report::http::register_import_confirm_routes(db_client);
    bridge_report::http::register_import_record_deletion_routes(db_client, cleanup_coordinator);
    bridge_report::http::register_word_import_routes(db_client, config);
    bridge_report::http::register_standard_routes(db_client, standards.registry);
    bridge_report::http::register_assessment_routes(db_client, standards.registry);
    bridge_report::http::register_workspace_routes(db_client, config);
    bridge_report::http::register_component_archive_routes(db_client, config.archive_root);
    bridge_report::http::register_component_inventory_routes(db_client, standards.registry);
    bridge_report::http::register_defect_thread_routes(db_client);
    bridge_report::http::register_inspection_year_deletion_routes(db_client, cleanup_coordinator);

    std::cout << "Bridge Report C++ backend listening on "
              << config.host << ":" << config.port << "\n";

    const std::string log_path = "logs";
    bridge_report::runtime::ensure_log_directory(log_path);

    drogon::app()
        .addListener(config.host, config.port)
        .setClientMaxBodySize(bridge_report::config::word_upload_request_max_bytes(config))
        .setLogPath(log_path)
        .setLogLevel(trantor::Logger::kInfo)
        .run();

    return 0;
}
