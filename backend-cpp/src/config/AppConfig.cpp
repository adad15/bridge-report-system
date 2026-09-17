#include "bridge_report/config/AppConfig.hpp"

#include <cstdlib>
#include <fstream>
#include <limits>

#include <json/json.h>

namespace bridge_report::config {

namespace {

std::string get_string_or_default(const Json::Value& object, const char* key, std::string fallback) {
    if (!object.isObject() || !object.isMember(key) || !object[key].isString()) {
        return fallback;
    }
    return object[key].asString();
}

int get_int_or_default(const Json::Value& object, const char* key, int fallback) {
    if (!object.isObject() || !object.isMember(key) || !object[key].isInt()) {
        return fallback;
    }
    return object[key].asInt();
}

int get_positive_int_or_default(const Json::Value& object, const char* key, int fallback) {
    const auto value = get_int_or_default(object, key, fallback);
    return value > 0 ? value : fallback;
}

std::size_t get_size_or_default(const Json::Value& object, const char* key, const std::size_t fallback) {
    if (!object.isObject() || !object.isMember(key) || !object[key].isUInt64()) {
        return fallback;
    }
    const auto value = object[key].asUInt64();
    return value == 0 ? fallback : static_cast<std::size_t>(value);
}

void apply_environment_overrides(AppConfig& config) {
    if (const auto* standards_root = std::getenv("BRIDGE_REPORT_STANDARDS_ROOT");
        standards_root != nullptr && standards_root[0] != '\0') {
        config.standards_root = standards_root;
    }
}

}  // 匿名命名空间

AppConfig load_app_config(const std::filesystem::path& path) {
    AppConfig config;

    // 配置文件缺失或 JSON 无效时，调用方继续使用结构体默认值。
    std::ifstream input(path);
    if (!input.good()) {
        apply_environment_overrides(config);
        return config;
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors)) {
        apply_environment_overrides(config);
        return config;
    }

    const auto& cpp_server = root["cpp_server"];
    config.host = get_string_or_default(cpp_server, "host", config.host);
    config.port = get_int_or_default(cpp_server, "port", config.port);

    const auto& python_tools = root["python_tools"];
    config.python_tools_base_url = get_string_or_default(
        python_tools,
        "base_url",
        config.python_tools_base_url
    );

    const auto& archive = root["archive"];
    config.archive_root = get_string_or_default(
        archive,
        "root",
        config.archive_root.generic_string()
    );
    config.word_upload_max_bytes = get_size_or_default(
        archive,
        "word_upload_max_bytes",
        config.word_upload_max_bytes
    );
    config.photo_upload_max_bytes = get_size_or_default(
        archive,
        "photo_upload_max_bytes",
        config.photo_upload_max_bytes
    );
    config.template_upload_max_bytes = get_size_or_default(
        archive,
        "template_upload_max_bytes",
        config.template_upload_max_bytes
    );
    config.cleanup_interval_seconds = get_positive_int_or_default(
        archive, "cleanup_interval_seconds", config.cleanup_interval_seconds);
    config.cleanup_batch_size = get_positive_int_or_default(
        archive, "cleanup_batch_size", config.cleanup_batch_size);
    config.cleanup_claim_timeout_seconds = get_positive_int_or_default(
        archive, "cleanup_claim_timeout_seconds", config.cleanup_claim_timeout_seconds);
    config.cleanup_retry_base_seconds = get_positive_int_or_default(
        archive, "cleanup_retry_base_seconds", config.cleanup_retry_base_seconds);
    config.cleanup_retry_max_seconds = get_positive_int_or_default(
        archive, "cleanup_retry_max_seconds", config.cleanup_retry_max_seconds);
    if (config.cleanup_retry_max_seconds < config.cleanup_retry_base_seconds) {
        config.cleanup_retry_max_seconds = config.cleanup_retry_base_seconds;
    }

    const auto& temporary_storage = root["temporary_storage"];
    config.temporary_word_root = get_string_or_default(
        temporary_storage,
        "root",
        config.temporary_word_root.generic_string()
    );
    config.failed_word_retention_hours = get_positive_int_or_default(
        temporary_storage,
        "failed_word_retention_hours",
        config.failed_word_retention_hours
    );
    config.temporary_report_root = get_string_or_default(
        temporary_storage,
        "report_root",
        config.temporary_report_root.generic_string()
    );
    config.report_retention_hours = get_positive_int_or_default(
        temporary_storage,
        "report_retention_hours",
        config.report_retention_hours
    );
    config.report_job_history_days = get_positive_int_or_default(
        temporary_storage,
        "report_job_history_days",
        config.report_job_history_days
    );
    config.report_field_update_timeout_seconds = get_positive_int_or_default(
        temporary_storage,
        "report_field_update_timeout_seconds",
        config.report_field_update_timeout_seconds
    );

    const auto& standards = root["standards"];
    config.standards_root = get_string_or_default(
        standards,
        "root",
        config.standards_root.generic_string()
    );

    const auto& postgres = root["postgres"];
    config.postgres.host = get_string_or_default(postgres, "host", config.postgres.host);
    config.postgres.port = get_int_or_default(postgres, "port", config.postgres.port);
    config.postgres.database = get_string_or_default(
        postgres,
        "database",
        config.postgres.database
    );
    config.postgres.user = get_string_or_default(postgres, "user", config.postgres.user);
    config.postgres.password = get_string_or_default(postgres, "password", config.postgres.password);

    const auto& map = root["map"];
    config.map.js_key = get_string_or_default(map, "js_key", config.map.js_key);
    config.map.security_js_code =
        get_string_or_default(map, "security_js_code", config.map.security_js_code);
    config.map.web_service_key =
        get_string_or_default(map, "web_service_key", config.map.web_service_key);

    apply_environment_overrides(config);
    return config;
}

std::size_t word_upload_request_max_bytes(const AppConfig& config) noexcept {
    constexpr std::size_t multipart_envelope_allowance = 1024ULL * 1024ULL;
    const auto maximum = std::numeric_limits<std::size_t>::max();
    if (config.word_upload_max_bytes > maximum - multipart_envelope_allowance) {
        return maximum;
    }
    return config.word_upload_max_bytes + multipart_envelope_allowance;
}

}  // 命名空间 bridge_report::config
