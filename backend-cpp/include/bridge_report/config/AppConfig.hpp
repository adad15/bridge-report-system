#pragma once

#include <filesystem>
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
    PostgresConfig postgres{};
};

/**
 * @brief 从 JSON 配置文件加载应用配置；缺失或解析失败时保留默认值。
 */
AppConfig load_app_config(const std::filesystem::path& path);

}  // 命名空间 bridge_report::config
