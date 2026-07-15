#include <filesystem>
#include <fstream>

#include <drogon/drogon.h>
#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/http/Cors.hpp"
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
  "postgres": {
    "host": "127.0.0.1",
    "port": 15432,
    "database": "bridge_report_test",
    "user": "bridge_report_tester",
    "password": "secret"
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
    EXPECT_EQ(config.postgres.host, "127.0.0.1");
    EXPECT_EQ(config.postgres.port, 15432);
    EXPECT_EQ(config.postgres.database, "bridge_report_test");
    EXPECT_EQ(config.postgres.user, "bridge_report_tester");
    EXPECT_EQ(config.postgres.password, "secret");
}

TEST(AppConfigTest, UsesDefaultsWhenConfigFileDoesNotExist) {
    const auto config = bridge_report::config::load_app_config("missing-local-config.json");

    EXPECT_EQ(config.host, "127.0.0.1");
    EXPECT_EQ(config.port, 18080);
    EXPECT_EQ(config.python_tools_base_url, "http://127.0.0.1:18081");
    EXPECT_EQ(config.archive_root.generic_string(), "archive");
    EXPECT_EQ(config.postgres.host, "127.0.0.1");
    EXPECT_EQ(config.postgres.port, 5432);
    EXPECT_EQ(config.postgres.database, "bridge_report_system");
    EXPECT_EQ(config.postgres.user, "bridge_report");
    EXPECT_EQ(config.postgres.password, "bridge_report_dev");
}

TEST(RuntimePathsTest, CreatesMissingLogDirectory) {
    const auto log_path = std::filesystem::temp_directory_path() / "bridge_report_test_logs";
    std::filesystem::remove_all(log_path);

    bridge_report::runtime::ensure_log_directory(log_path);

    EXPECT_TRUE(std::filesystem::is_directory(log_path));

    std::filesystem::remove_all(log_path);
}

TEST(CorsTest, AppliesLocalFrontendCorsHeaders) {
    auto response = drogon::HttpResponse::newHttpResponse();

    bridge_report::http::apply_local_dev_cors_headers(response);

    EXPECT_EQ(response->getHeader("Access-Control-Allow-Origin"), "http://127.0.0.1:5173");
    EXPECT_EQ(response->getHeader("Access-Control-Allow-Methods"), "GET, PUT, POST, DELETE, OPTIONS");
    EXPECT_EQ(
        response->getHeader("Access-Control-Allow-Headers"),
        "Content-Type, Authorization, X-Edit-Lock-Token"
    );
}
