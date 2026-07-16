#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/WordImportRepository.hpp"

namespace bridge_report::http {

struct PythonParseError {
    std::string code;
    std::string message;
};

Json::Value build_python_word_request(
    const db::WordImportContext& context,
    const Json::Value& body,
    const std::filesystem::path& temporary_photo_output_dir
);

Json::Value extract_python_parse_data(const Json::Value& response_body);

std::optional<PythonParseError> extract_python_parse_error(const Json::Value& response_body);

void register_word_import_routes(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config
);

}  // namespace bridge_report::http
