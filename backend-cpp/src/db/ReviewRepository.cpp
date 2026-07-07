#include "bridge_report/db/ReviewRepository.hpp"

#include <optional>
#include <utility>

namespace bridge_report::db {

namespace {

std::optional<std::string> optional_text(const drogon::orm::Row& row, const std::string& column) {
    const auto field = row[column];
    if (field.isNull()) {
        return std::nullopt;
    }
    return field.as<std::string>();
}

}  // 匿名命名空间

ReviewRepository::ReviewRepository(drogon::orm::DbClientPtr db_client) : db_client_(std::move(db_client)) {}

std::vector<review::BridgeSummary> ReviewRepository::list_bridges() {
    const auto result = db_client_->execSqlSync(
        "select id, system_number, bridge_name, route_name, status "
        "from bridges "
        "order by system_number"
    );

    std::vector<review::BridgeSummary> bridges;
    bridges.reserve(result.size());
    for (const auto& row : result) {
        review::BridgeSummary summary;
        summary.id = row["id"].as<std::string>();
        summary.system_number = row["system_number"].as<std::string>();
        summary.bridge_name = row["bridge_name"].as<std::string>();
        summary.route_name = optional_text(row, "route_name");
        summary.status = row["status"].as<std::string>();
        bridges.push_back(std::move(summary));
    }
    return bridges;
}

std::vector<review::InspectionYearSummary> ReviewRepository::list_inspection_years(const std::string& bridge_id) {
    const auto result = db_client_->execSqlSync(
        "select id, system_number, inspection_year, status, version_number, is_current "
        "from inspection_years "
        "where bridge_id = $1::uuid "
        "order by inspection_year desc, version_number desc",
        bridge_id
    );

    std::vector<review::InspectionYearSummary> years;
    years.reserve(result.size());
    for (const auto& row : result) {
        review::InspectionYearSummary summary;
        summary.id = row["id"].as<std::string>();
        summary.system_number = row["system_number"].as<std::string>();
        summary.inspection_year = row["inspection_year"].as<int>();
        summary.status = row["status"].as<std::string>();
        summary.version_number = row["version_number"].as<int>();
        summary.is_current = row["is_current"].as<bool>();
        years.push_back(std::move(summary));
    }
    return years;
}

std::vector<review::ImportRecordSummary> ReviewRepository::list_import_records(const std::string& bridge_id) {
    const auto result = db_client_->execSqlSync(
        "select id, system_number, import_name, source_type, import_status, "
        "inspection_year_id, importer_name, created_at::text "
        "from import_records "
        "where bridge_id = $1::uuid "
        "order by created_at desc",
        bridge_id
    );

    std::vector<review::ImportRecordSummary> records;
    records.reserve(result.size());
    for (const auto& row : result) {
        review::ImportRecordSummary summary;
        summary.id = row["id"].as<std::string>();
        summary.system_number = row["system_number"].as<std::string>();
        summary.import_name = row["import_name"].as<std::string>();
        summary.source_type = row["source_type"].as<std::string>();
        summary.import_status = row["import_status"].as<std::string>();
        summary.inspection_year_id = optional_text(row, "inspection_year_id");
        summary.importer_name = optional_text(row, "importer_name");
        summary.created_at = row["created_at"].as<std::string>();
        records.push_back(std::move(summary));
    }
    return records;
}

}  // 命名空间 bridge_report::db
