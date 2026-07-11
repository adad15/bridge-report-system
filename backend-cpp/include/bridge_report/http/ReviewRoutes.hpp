#pragma once

#include <filesystem>
#include <optional>

#include <drogon/orm/DbClient.h>

namespace bridge_report::http {

/**
 * @brief 注册桥梁导航与校对工作台 API：
 *   GET /api/bridges
 *   GET /api/bridges/{bridge_id}/inspection-years
 *   GET /api/bridges/{bridge_id}/import-records
 *   GET /api/import-records/{import_record_id}/review
 *   PUT /api/import-records/{import_record_id}/review-draft
 *   POST /api/import-records/{import_record_id}/cancel
 */
std::optional<std::filesystem::path> resolve_photo_content_path(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& storage_relative_path
);

void register_review_routes(
    const drogon::orm::DbClientPtr& db_client,
    const std::filesystem::path& archive_root
);

}  // 命名空间 bridge_report::http
