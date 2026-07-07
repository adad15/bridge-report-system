#pragma once

#include <optional>
#include <string>

#include <json/value.h>

namespace bridge_report::review {

/**
 * @brief 桥梁导航摘要，用于 GET /api/bridges 列表展示。
 */
struct BridgeSummary {
    std::string id;
    std::string system_number;
    std::string bridge_name;
    std::optional<std::string> route_name;
    std::string status;

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
};

}  // 命名空间 bridge_report::review
