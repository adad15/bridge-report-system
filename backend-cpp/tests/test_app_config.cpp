#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/runtime/RuntimePaths.hpp"

namespace {

std::filesystem::path write_config_file() {
    const auto path = std::filesystem::temp_directory_path() / "bridge_report_test_config.json";
    std::ofstream out(path);
    out << R"json({
  "cpp_server": {
    "host": "127.0.0.1",
    "port": 19080
  },
  "python_tools": {
    "base_url": "http://127.0.0.1:19081"
  },
  "archive": {
    "root": "test-archive"
  }
})json";
    return path;
}

}  // namespace

TEST(AppConfigTest, LoadsConfiguredPortsAndArchiveRoot) {
    const auto path = write_config_file();

    const auto config = bridge_report::config::load_app_config(path);

    EXPECT_EQ(config.host, "127.0.0.1");
    EXPECT_EQ(config.port, 19080);
    EXPECT_EQ(config.python_tools_base_url, "http://127.0.0.1:19081");
    EXPECT_EQ(config.archive_root.generic_string(), "test-archive");
}

TEST(AppConfigTest, UsesDefaultsWhenConfigFileDoesNotExist) {
    const auto config = bridge_report::config::load_app_config("missing-local-config.json");

    EXPECT_EQ(config.host, "127.0.0.1");
    EXPECT_EQ(config.port, 18080);
    EXPECT_EQ(config.python_tools_base_url, "http://127.0.0.1:18081");
    EXPECT_EQ(config.archive_root.generic_string(), "archive");
}

TEST(RuntimePathsTest, CreatesMissingLogDirectory) {
    const auto log_path = std::filesystem::temp_directory_path() / "bridge_report_test_logs";
    std::filesystem::remove_all(log_path);

    bridge_report::runtime::ensure_log_directory(log_path);

    EXPECT_TRUE(std::filesystem::is_directory(log_path));

    std::filesystem::remove_all(log_path);
}
