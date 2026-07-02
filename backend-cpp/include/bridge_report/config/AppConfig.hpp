#pragma once

#include <filesystem>
#include <string>

namespace bridge_report::config {

struct AppConfig {
    std::string host{"127.0.0.1"};
    int port{18080};
    std::string python_tools_base_url{"http://127.0.0.1:18081"};
    std::filesystem::path archive_root{"archive"};
};

AppConfig load_app_config(const std::filesystem::path& path);

}  // namespace bridge_report::config
