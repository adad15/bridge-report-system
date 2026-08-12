#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/archive/SourceDbReference.hpp"
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

/// 接口同步导入的 Python 请求体。产出的响应与 Word 那条路同形状，
/// 差别只在解析器读的是本机离线库而不是 docx。
Json::Value build_python_source_request(
    const db::WordImportContext& context,
    const Json::Value& body,
    const archive::SourceDbReference& reference,
    const std::filesystem::path& temporary_photo_output_dir
);

/// 该导入记录是否走接口同步（读来源软件离线库）而不是 Word 解析。
[[nodiscard]] bool is_source_db_import(const db::WordImportContext& context);

void register_word_import_routes(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config
);

}  // namespace bridge_report::http
