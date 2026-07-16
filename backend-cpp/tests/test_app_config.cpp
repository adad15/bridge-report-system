#include <filesystem>
#include <fstream>
#include <limits>

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
    "root": "test-archive",
    "word_upload_max_bytes": 1048576,
    "cleanup_interval_seconds": 60,
    "cleanup_batch_size": 10,
    "cleanup_claim_timeout_seconds": 120,
    "cleanup_retry_base_seconds": 30,
    "cleanup_retry_max_seconds": 3600
  },
  "temporary_storage": {
    "root": "test-runtime/word-imports",
    "failed_word_retention_hours": 48
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
    EXPECT_EQ(config.temporary_word_root.generic_string(), "test-runtime/word-imports");
    EXPECT_EQ(config.failed_word_retention_hours, 48);
    EXPECT_EQ(config.word_upload_max_bytes, 1048576u);
    EXPECT_EQ(config.cleanup_interval_seconds, 60);
    EXPECT_EQ(config.cleanup_batch_size, 10);
    EXPECT_EQ(config.cleanup_claim_timeout_seconds, 120);
    EXPECT_EQ(config.cleanup_retry_base_seconds, 30);
    EXPECT_EQ(config.cleanup_retry_max_seconds, 3600);
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
    EXPECT_EQ(config.temporary_word_root.generic_string(), "runtime/temp/word-imports");
    EXPECT_EQ(config.failed_word_retention_hours, 24);
    EXPECT_EQ(config.word_upload_max_bytes, 256u * 1024u * 1024u);
    EXPECT_EQ(config.cleanup_interval_seconds, 300);
    EXPECT_EQ(config.cleanup_batch_size, 25);
    EXPECT_EQ(config.cleanup_claim_timeout_seconds, 900);
    EXPECT_EQ(config.cleanup_retry_base_seconds, 300);
    EXPECT_EQ(config.cleanup_retry_max_seconds, 86400);
    EXPECT_EQ(config.postgres.host, "127.0.0.1");
    EXPECT_EQ(config.postgres.port, 5432);
    EXPECT_EQ(config.postgres.database, "bridge_report_system");
    EXPECT_EQ(config.postgres.user, "bridge_report");
    EXPECT_EQ(config.postgres.password, "bridge_report_dev");
}

TEST(AppConfigTest, AllowsMultipartEnvelopeBeyondConfiguredWordFileLimit) {
    bridge_report::config::AppConfig config;
    config.word_upload_max_bytes = 256u * 1024u * 1024u;

    EXPECT_EQ(
        bridge_report::config::word_upload_request_max_bytes(config),
        257u * 1024u * 1024u
    );

    config.word_upload_max_bytes = (std::numeric_limits<std::size_t>::max)();
    EXPECT_EQ(
        bridge_report::config::word_upload_request_max_bytes(config),
        (std::numeric_limits<std::size_t>::max)()
    );
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
