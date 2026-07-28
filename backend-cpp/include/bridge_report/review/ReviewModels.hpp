#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <json/value.h>

namespace bridge_report::review {

struct ReviewStatistics;

/**
 * @brief 桥梁导航摘要，用于 GET /api/bridges 列表展示。
 */
struct BridgeSummary {
    std::string id;
    std::string system_number;
    std::string bridge_name;
    std::optional<std::string> route_name;
    std::string status;
    std::optional<std::string> bridge_scale;
    std::optional<int> latest_inspection_year;
    std::optional<double> latest_overall_score;
    std::optional<std::string> latest_overall_grade;
    int pending_count{0};

    Json::Value to_json() const;
};

/**
 * @brief 年度检查摘要，用于 GET /api/bridges/{bridge_id}/inspection-years 列表展示。
 */
struct InspectionYearSummary {
    std::string id;
    std::string system_number;
    int inspection_year{0};
    std::string status;
    int version_number{0};
    bool is_current{false};

    Json::Value to_json() const;
};

/**
 * @brief 导入记录摘要，用于 GET /api/bridges/{bridge_id}/import-records 列表展示。
 */
struct ImportRecordSummary {
    std::string id;
    std::string system_number;
    std::string import_name;
    std::string source_type;
    std::string import_status;
    std::optional<std::string> inspection_year_id;
    std::optional<std::string> importer_name;
    std::string created_at;
    std::optional<std::string> edit_lock_owner_username;
    std::optional<std::string> edit_lock_owner_display_name;
    std::optional<std::string> edit_lock_acquired_at;
    std::optional<std::string> edit_lock_expires_at;

    Json::Value to_json() const;
};

/**
 * @brief 导入记录详情，用于 GET /api/import-records/{import_record_id}/review。
 *
 * 联查 import_records + bridges + inspection_years（左联，年度可空）。
 * parsed_result_json 保留为原始文本，由调用方按需解析为 Json::Value。
 */
struct ImportRecordDetail {
    // import_records 字段
    std::string id;
    std::string system_number;
    std::string bridge_id;
    std::optional<std::string> inspection_year_id;
    std::string import_name;
    std::string source_type;
    std::string import_status;
    std::optional<std::string> importer_name;
    std::optional<std::string> importer_version;
    std::string parsed_result_json;
    std::string created_at;
    std::string updated_at;

    // bridges 字段
    std::string bridge_system_number;
    std::string bridge_name;
    std::optional<std::string> bridge_route_name;

    // inspection_years 字段（左联，可空）
    std::optional<std::string> inspection_year_system_number;
    std::optional<int> inspection_year;
    std::optional<std::string> inspection_year_status;
    std::optional<int> inspection_year_version_number;
    std::optional<bool> inspection_year_is_current;
    std::optional<std::string> technical_standard_package_id;
    std::optional<std::string> technical_standard_code;
    std::optional<std::string> technical_standard_name;
    std::optional<std::string> technical_standard_official_edition;
    std::optional<std::string> technical_standard_package_version;
    std::optional<std::string> rating_tree_version_id;
    std::optional<std::string> rating_tree_name;
    std::optional<std::string> rating_tree_package_version;
    std::optional<std::string> rating_tree_content_checksum;

    // 重开校对审计（迁移 004）：reopened_at 非空即处于重开态；
    // scope 为 'warnings_only'（仅警告病害可改）或 'full'（管理员全改）。
    std::optional<std::string> reopened_at;
    std::optional<std::string> reopened_by_username;
    std::optional<std::string> reopen_scope;
};

/**
 * @brief 组装 GET /api/import-records/{import_record_id}/review 的响应体。
 *
 * 纯函数：只做 JSON 形状拼装，不访问数据库。has_current_annual_facts 由调用方
 * （路由层）通过 ReviewRepository 查询得到后传入。
 */
Json::Value build_review_response(
    const ImportRecordDetail& detail,
    const Json::Value& parsed_result,
    const ReviewStatistics& statistics,
    bool has_current_annual_facts,
    std::string_view contract_compatibility
);

/**
 * @brief 解析导入记录的“有效检测年度”：记录已挂年度时优先使用挂载的年度；
 * 否则退化为已解析 JSON 中 inspection.inspection_year（须为合法整数）；两者都没有时返回 nullopt。
 *
 * 纯函数，供 GET /api/import-records/{id}/review 与 POST .../preflight-confirm 两个路由共用，
 * 用于决定 has_current_annual_facts 应以哪个年度查询。
 */
std::optional<int> resolve_effective_inspection_year(
    const ImportRecordDetail& detail,
    const Json::Value& parsed_result
);

}  // 命名空间 bridge_report::review
