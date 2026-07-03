#pragma once
// 主要用于定义桥梁报告系统 (bridge_report) 后端的应用程序配置信息
#include <filesystem>
#include <string>

namespace bridge_report::config {

struct PostgresConfig {
    std::string host{"127.0.0.1"};
    int port{5432};
    std::string database{"bridge_report_system"};
    std::string user{"bridge_report"};
    std::string password{"bridge_report_dev"};
};

struct AppConfig {
    std::string host{"127.0.0.1"};
    int port{18080};
    // C++ 后端会通过 HTTP 请求去调用另外一个运行在本机的 Python 工具服务
    std::string python_tools_base_url{"http://127.0.0.1:18081"};
    std::filesystem::path archive_root{"archive"};
    PostgresConfig postgres{};
};

AppConfig load_app_config(const std::filesystem::path& path);

}  // namespace bridge_report::config
