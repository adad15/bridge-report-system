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

}  // 命名空间 bridge_report::review
