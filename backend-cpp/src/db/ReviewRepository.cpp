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

std::optional<review::ImportRecordDetail> ReviewRepository::get_import_record_detail(
    const std::string& import_record_id
) {
    const auto result = db_client_->execSqlSync(
        "select "
        "ir.id, ir.system_number, ir.bridge_id, ir.inspection_year_id, "
        "ir.import_name, ir.source_type, ir.import_status, "
        "ir.importer_name, ir.importer_version, ir.parsed_result_json::text as parsed_result_json, "
        "ir.created_at::text as created_at, ir.updated_at::text as updated_at, "
        "b.system_number as bridge_system_number, b.bridge_name as bridge_name, b.route_name as bridge_route_name, "
        "iy.system_number as inspection_year_system_number, iy.inspection_year as inspection_year, "
        "iy.status as inspection_year_status, iy.version_number as inspection_year_version_number, "
        "iy.is_current as inspection_year_is_current "
        "from import_records ir "
        "join bridges b on b.id = ir.bridge_id "
        "left join inspection_years iy on iy.id = ir.inspection_year_id "
        "where ir.id = $1::uuid",
        import_record_id
    );

    if (result.empty()) {
        return std::nullopt;
    }

    const auto& row = result[0];
    review::ImportRecordDetail detail;
    detail.id = row["id"].as<std::string>();
    detail.system_number = row["system_number"].as<std::string>();
    detail.bridge_id = row["bridge_id"].as<std::string>();
    detail.inspection_year_id = optional_text(row, "inspection_year_id");
    detail.import_name = row["import_name"].as<std::string>();
    detail.source_type = row["source_type"].as<std::string>();
    detail.import_status = row["import_status"].as<std::string>();
    detail.importer_name = optional_text(row, "importer_name");
    detail.importer_version = optional_text(row, "importer_version");
    detail.parsed_result_json = row["parsed_result_json"].as<std::string>();
    detail.created_at = row["created_at"].as<std::string>();
    detail.updated_at = row["updated_at"].as<std::string>();

    detail.bridge_system_number = row["bridge_system_number"].as<std::string>();
    detail.bridge_name = row["bridge_name"].as<std::string>();
    detail.bridge_route_name = optional_text(row, "bridge_route_name");

    detail.inspection_year_system_number = optional_text(row, "inspection_year_system_number");
    const auto inspection_year_field = row["inspection_year"];
    detail.inspection_year = inspection_year_field.isNull()
        ? std::nullopt
        : std::make_optional(inspection_year_field.as<int>());
    detail.inspection_year_status = optional_text(row, "inspection_year_status");
    const auto version_number_field = row["inspection_year_version_number"];
    detail.inspection_year_version_number = version_number_field.isNull()
        ? std::nullopt
        : std::make_optional(version_number_field.as<int>());
    const auto is_current_field = row["inspection_year_is_current"];
    detail.inspection_year_is_current = is_current_field.isNull()
        ? std::nullopt
        : std::make_optional(is_current_field.as<bool>());

    return detail;
}

bool ReviewRepository::save_review_draft(const std::string& import_record_id, const std::string& parsed_json_text) {
    // 与 cancel_import_record 同一惯用法：把状态谓词放进 UPDATE，
    // 避免“处理器读到待校对 -> 并发取消/确认 -> 草稿仍写入”的 TOCTOU 竞态。
    const auto result = db_client_->execSqlSync(
        "update import_records "
        "set parsed_result_json = $2::jsonb, updated_at = now() "
        "where id = $1::uuid "
        "and import_status = '待校对' "
        "returning id",
        import_record_id,
        parsed_json_text
    );
    return !result.empty();
}

bool ReviewRepository::cancel_import_record(const std::string& import_record_id) {
    const auto result = db_client_->execSqlSync(
        "update import_records "
        "set import_status = '已取消', updated_at = now() "
        "where id = $1::uuid "
        "and import_status in ('已上传', '解析中', '待校对', '解析失败') "
        "returning id",
        import_record_id
    );
    return !result.empty();
}

bool ReviewRepository::has_current_annual_facts(const std::string& bridge_id, int inspection_year) {
    const auto result = db_client_->execSqlSync(
        "select exists("
        "select 1 from inspection_years "
        "where bridge_id = $1::uuid and inspection_year = $2 and is_current and status = '已确认'"
        ") as found",
        bridge_id,
        inspection_year
    );
    return !result.empty() && result[0]["found"].as<bool>();
}

}  // 命名空间 bridge_report::db
