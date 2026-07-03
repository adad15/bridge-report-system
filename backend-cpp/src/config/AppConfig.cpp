#include "bridge_report/config/AppConfig.hpp"

#include <fstream>

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

}  // namespace

AppConfig load_app_config(const std::filesystem::path& path) {
    AppConfig config;

    std::ifstream input(path);
    if (!input.good()) {
        return config;
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    // Json::parseFromStream 读取文件流 input，并将解析好的 JSON 树保存在 root 节点中
    if (!Json::parseFromStream(builder, input, &root, &errors)) {
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

    return config;
}

}  // namespace bridge_report::config
