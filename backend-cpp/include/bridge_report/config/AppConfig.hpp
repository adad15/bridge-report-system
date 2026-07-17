#pragma once

#include <filesystem>
#include <cstddef>
#include <string>

namespace bridge_report::config {

/**
 * @brief PostgreSQL 数据库连接配置。
 */
struct PostgresConfig {
    std::string host{"127.0.0.1"};
    int port{5432};
    std::string database{"bridge_report_system"};
    std::string user{"bridge_report"};
    std::string password{"bridge_report_dev"};
};

/**
 * @brief C++ 后端启动所需的应用配置。
 */
struct AppConfig {
    std::string host{"127.0.0.1"};
    int port{18080};
    std::string python_tools_base_url{"http://127.0.0.1:18081"};
    std::filesystem::path archive_root{"archive"};
    std::filesystem::path standards_root{"standards"};
    std::filesystem::path temporary_word_root{"runtime/temp/word-imports"};
    int failed_word_retention_hours{24};
    std::size_t word_upload_max_bytes{256ULL * 1024ULL * 1024ULL};
    int cleanup_interval_seconds{300};
    int cleanup_batch_size{25};
    int cleanup_claim_timeout_seconds{900};
    int cleanup_retry_base_seconds{300};
    int cleanup_retry_max_seconds{86400};
    PostgresConfig postgres{};
};

/**
 * @brief 从 JSON 配置文件加载应用配置；缺失或解析失败时保留默认值。
 */
AppConfig load_app_config(const std::filesystem::path& path);

/**
 * @brief Drogon 接收 multipart 上传时使用的请求体上限。
 *
 * Word 文件大小上限只计算文件内容；HTTP multipart 还包含边界、字段和文件名，
 * 因此框架层必须额外预留封装空间，业务路由再执行精确的文件大小校验。
 */
std::size_t word_upload_request_max_bytes(const AppConfig& config) noexcept;

}  // 命名空间 bridge_report::config
