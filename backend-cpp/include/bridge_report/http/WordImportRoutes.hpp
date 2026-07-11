#pragma once

#include <filesystem>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/WordImportRepository.hpp"

namespace bridge_report::http {

Json::Value build_python_word_request(
    const db::WordImportContext& context,
    const Json::Value& body,
    const std::filesystem::path& temporary_photo_output_dir
);

void register_word_import_routes(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config
);

}  // namespace bridge_report::http
