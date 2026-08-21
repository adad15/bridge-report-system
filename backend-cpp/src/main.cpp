#include <filesystem>
#include <functional>
#include <iostream>
#include <system_error>
#include <memory>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Exception.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/AuthRepository.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/RatingTreeRepository.hpp"
#include "bridge_report/db/StandardRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/AssessmentRoutes.hpp"
#include "bridge_report/http/BridgeAdministrationRoutes.hpp"
#include "bridge_report/http/ComponentArchiveRoutes.hpp"
#include "bridge_report/http/ComponentInventoryRoutes.hpp"
#include "bridge_report/http/Cors.hpp"
#include "bridge_report/http/DefectThreadRoutes.hpp"
#include "bridge_report/http/EditLockRoutes.hpp"
#include "bridge_report/http/DefectMatchingRoutes.hpp"
#include "bridge_report/http/DefectPhotoRoutes.hpp"
#include "bridge_report/http/ImportBindingRoutes.hpp"
#include "bridge_report/http/RoutePreflightAudit.hpp"
#include "bridge_report/http/ImportConfirmRoutes.hpp"
#include "bridge_report/http/ImportRecordDeletionRoutes.hpp"
#include "bridge_report/http/InspectionYearDeletionRoutes.hpp"
#include "bridge_report/http/RatingTreeRoutes.hpp"
#include "bridge_report/http/ReviewRoutes.hpp"
#include "bridge_report/http/StandardRoutes.hpp"
#include "bridge_report/http/WordImportRoutes.hpp"
#include "bridge_report/http/WorkspaceRoutes.hpp"
#include "bridge_report/runtime/RuntimePaths.hpp"
#include "bridge_report/rating_tree/RatingTreeCompiler.hpp"
#include "bridge_report/rating_tree/RatingTreePackageLoader.hpp"
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
        if (package_root.lexically_normal().generic_string().find(
                "/rating-tree/") != std::string::npos) {
            continue;
        }
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

std::vector<bridge_report::rating_tree::EffectiveRatingTree>
load_effective_rating_trees(
    const std::filesystem::path& standards_root,
    std::vector<bridge_report::standards::StandardIssue>& issues) {
    bridge_report::rating_tree::RatingTreePackageLoader tree_loader;
    bridge_report::standards::StandardPackageLoader standard_loader;
    bridge_report::rating_tree::RatingTreeCompiler compiler;
    std::vector<bridge_report::rating_tree::EffectiveRatingTree> trees;

    for (const auto& package_root : tree_loader.discover(standards_root)) {
        auto extension = tree_loader.load(package_root);
        if (!extension.ok()) {
            for (const auto& tree_issue : extension.issues) {
                issues.push_back({tree_issue.code, tree_issue.message});
                std::cerr << "评定树加载失败 [" << tree_issue.code << "]："
                          << tree_issue.message << "\n";
            }
            continue;
        }

        const auto find_reference = [&](const std::string& source_type)
            -> std::optional<std::filesystem::path> {
            for (const auto& [_, source] : extension.package->sources) {
                if (source.source_type != source_type) continue;
                auto reference = std::filesystem::path(source.reference);
                if (!reference.empty() &&
                    *reference.begin() == std::filesystem::path("standards")) {
                    return standards_root.parent_path() / reference;
                }
                return standards_root / reference;
            }
            return std::nullopt;
        };
        const auto technical_path = find_reference("technical_condition");
        const auto maintenance_path = find_reference("maintenance");
        if (!technical_path.has_value() || !maintenance_path.has_value()) {
            bridge_report::standards::StandardIssue issue{
                "rating_tree_source_reference_missing",
                "评定树必须明确引用 H21 技术评定规范和 JTG 5120 养护规范。",
            };
            std::cerr << "评定树编译失败 [" << issue.code << "]："
                      << issue.message << "\n";
            issues.push_back(std::move(issue));
            continue;
        }

        auto technical = standard_loader.load(*technical_path);
        auto maintenance = standard_loader.load(*maintenance_path);
        if (!technical.ok() || !maintenance.ok()) {
            bridge_report::standards::StandardIssue issue{
                "rating_tree_source_package_invalid",
                "评定树引用的规范包缺失或未通过完整性校验。",
            };
            std::cerr << "评定树编译失败 [" << issue.code << "]："
                      << issue.message << "\n";
            issues.push_back(std::move(issue));
            continue;
        }

        auto compiled = compiler.compile(
            *technical.package, &*maintenance.package, *extension.package);
        if (!compiled.ok()) {
            for (const auto& tree_issue : compiled.issues) {
                issues.push_back({tree_issue.code, tree_issue.message});
                std::cerr << "评定树编译失败 [" << tree_issue.code << "]："
                          << tree_issue.message << "\n";
            }
            continue;
        }
        trees.push_back(std::move(*compiled.tree));
    }
    return trees;
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
    // 配置里的归档根通常是相对路径，按进程工作目录解析。从不同目录启动后端会写到
    // 不同的归档目录，症状是"照片时有时无"且极难定位，故把解析后的绝对路径也报出来。
    std::error_code archive_root_error;
    const auto absolute_archive_root =
        std::filesystem::absolute(config.archive_root, archive_root_error);
    body["archive_root_absolute"] =
        archive_root_error ? config.archive_root.generic_string()
                           : absolute_archive_root.generic_string();
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
                const auto rating_tree_rows = db_client->execSqlSync(
                    "select "
                    "(select count(*) from rating_tree_versions where status='published') "
                    "as published_count,"
                    "(select count(*) from rating_tree_versions where status='failed') "
                    "as failed_count,"
                    "(select count(*) from rating_tree_binding_diagnostics) "
                    "as binding_issue_count");

                Json::Value body;
                body["status"] = "ok";
                body["database"] = "reachable";
                if (!rating_tree_rows.empty()) {
                    const auto published_count =
                        rating_tree_rows[0]["published_count"].as<std::int64_t>();
                    const auto failed_count =
                        rating_tree_rows[0]["failed_count"].as<std::int64_t>();
                    const auto binding_issue_count =
                        rating_tree_rows[0]["binding_issue_count"].as<std::int64_t>();
                    body["rating_tree_status"] =
                        published_count > 0 && failed_count == 0 &&
                                binding_issue_count == 0
                            ? "ok"
                            : "degraded";
                    body["rating_tree_published_count"] =
                        static_cast<Json::Int64>(published_count);
                    body["rating_tree_failed_count"] =
                        static_cast<Json::Int64>(failed_count);
                    body["rating_tree_binding_issue_count"] =
                        static_cast<Json::Int64>(binding_issue_count);
                }
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
    auto rating_trees =
        load_effective_rating_trees(config.standards_root, standards.issues);

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

        bridge_report::db::RatingTreeRepository rating_tree_repository(db_client);
        for (const auto& tree : rating_trees) {
            const auto outcome =
                rating_tree_repository.sync_published_tree(tree);
            if (outcome.status ==
                    bridge_report::db::RatingTreeSyncStatus::Inserted ||
                outcome.status ==
                    bridge_report::db::RatingTreeSyncStatus::Unchanged) {
                continue;
            }
            // 来源规范包被停用是预期状态，不是故障：只保留当前规范版本可用是正常
            // 运维，旧评定树因此无法再同步，但它们早已发布在库里、历史年度照常可用
            // （下面那句"既有已发布版本未被覆盖"说的就是这个）。按故障报的话，每次
            // 启动都刷几行，真正的同步失败反而被淹掉。
            if (outcome.status ==
                    bridge_report::db::RatingTreeSyncStatus::SourcePackageDisabled) {
                // 与本函数其余启动诊断一致用 cerr：cout 是全缓冲的，重定向到文件时
                // 这几行会一直压在缓冲区里，服务不退出就永远看不到。
                std::cerr << "评定树跳过同步：" << tree.version.tree_code << " "
                          << tree.version.package_version << "，来源规范包 "
                          << outcome.blocking_source_package
                          << " 已停用；库中既有的已发布版本保持不变。\n";
                continue;
            }
            const auto code =
                outcome.status ==
                        bridge_report::db::RatingTreeSyncStatus::ChecksumConflict
                ? "rating_tree_database_checksum_conflict"
                : outcome.status ==
                        bridge_report::db::RatingTreeSyncStatus::SourcePackageNotFound
                ? "rating_tree_source_package_not_synchronized"
                : "rating_tree_database_sync_failed";
            bridge_report::standards::StandardIssue issue{
                code,
                "有效评定树未能同步发布到数据库，既有已发布版本未被覆盖。",
            };
            std::cerr << "评定树数据库同步失败 [" << issue.code << "]："
                      << tree.version.tree_code << " "
                      << tree.version.package_version << "；"
                      << issue.message << "\n";
            standards.issues.push_back(std::move(issue));
        }
        const auto backfill =
            rating_tree_repository.backfill_unique_profile_versions();
        if (backfill.ambiguous_profile_count != 0) {
            bridge_report::standards::StandardIssue issue{
                "rating_tree_profile_backfill_ambiguous",
                "部分历史规范组合对应多个已发布评定树，未自动回填。",
            };
            std::cerr << "历史评定树回填失败 [" << issue.code << "]："
                      << backfill.ambiguous_profile_count << " 个规范组合；"
                      << issue.message << "\n";
            standards.issues.push_back(std::move(issue));
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
    bridge_report::http::register_import_confirm_routes(db_client, standards.registry);
    bridge_report::http::register_import_binding_routes(db_client);
    bridge_report::http::register_defect_matching_routes(db_client);
    bridge_report::http::register_defect_photo_routes(db_client, config);
    bridge_report::http::register_import_record_deletion_routes(db_client, cleanup_coordinator);
    bridge_report::http::register_word_import_routes(db_client, config);
    bridge_report::http::register_standard_routes(db_client, standards.registry);
    bridge_report::http::register_assessment_routes(db_client, standards.registry);
    bridge_report::http::register_workspace_routes(db_client, config);
    bridge_report::http::register_component_archive_routes(db_client, config.archive_root);
    bridge_report::http::register_component_inventory_routes(db_client, standards.registry);
    bridge_report::http::register_defect_thread_routes(db_client);
    bridge_report::http::register_inspection_year_deletion_routes(db_client, cleanup_coordinator);
    bridge_report::http::register_rating_tree_routes(db_client);

    // 路由自检：能改数据的接口必须同时注册 OPTIONS 预检。
    //
    // 缺了会怎样：浏览器发改写请求前先发 OPTIONS 预检，预检 404 则真正的请求根本
    // 不会发出——前端只拿到 fetch 的网络错误（报一句笼统的"操作失败"），后端日志
    // 里一片空白，测试也照样全绿，三条线索全是死的。bind-multi 上线时就是这么坏的，
    // 最后是用户在界面上点出来才发现。
    //
    // 这里拒绝启动而不是记一条日志：路径写死在代码里，缺预检就是缺预检，重启一百次
    // 也不会自己好；带病启动只是把问题推迟到浏览器里，再以最没线索的形式冒出来。
    // 本地开发后端，快速失败的代价不过是重编一次。
    if (const auto missing = bridge_report::http::paths_missing_preflight(
            drogon::app().getHandlersInfo());
        !missing.empty()) {
        std::cerr << "路由自检失败：以下写接口缺少 OPTIONS 预检处理器，"
                     "浏览器的跨域预检会得到 404，真正的请求根本发不出去：\n";
        for (const auto& path : missing) {
            std::cerr << "    " << path << "\n";
        }
        std::cerr << "修法：改用成对注册（见 ImportBindingRoutes.cpp 的 "
                     "register_post_route），它会把 POST 与其预检一起注册。\n"
                     "后端未启动。\n";
        return 1;
    }
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
