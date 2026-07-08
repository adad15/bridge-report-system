#include "bridge_report/review/ReviewModels.hpp"

#include "bridge_report/review/ReviewStatistics.hpp"

namespace bridge_report::review {

Json::Value BridgeSummary::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["system_number"] = system_number;
    json["bridge_name"] = bridge_name;
    json["route_name"] = route_name.has_value() ? Json::Value(*route_name) : Json::Value(Json::nullValue);
    json["status"] = status;
    return json;
}

Json::Value InspectionYearSummary::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["system_number"] = system_number;
    json["inspection_year"] = inspection_year;
    json["status"] = status;
    json["version_number"] = version_number;
    json["is_current"] = is_current;
    return json;
}

Json::Value ImportRecordSummary::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["system_number"] = system_number;
    json["import_name"] = import_name;
    json["source_type"] = source_type;
    json["import_status"] = import_status;
    json["inspection_year_id"] =
        inspection_year_id.has_value() ? Json::Value(*inspection_year_id) : Json::Value(Json::nullValue);
    json["importer_name"] =
        importer_name.has_value() ? Json::Value(*importer_name) : Json::Value(Json::nullValue);
    json["created_at"] = created_at;
    return json;
}

Json::Value build_review_response(
    const ImportRecordDetail& detail,
    const Json::Value& parsed_result,
    const ReviewStatistics& statistics,
    bool has_current_annual_facts
) {
    Json::Value import_record;
    import_record["id"] = detail.id;
    import_record["system_number"] = detail.system_number;
    import_record["import_name"] = detail.import_name;
    import_record["source_type"] = detail.source_type;
    import_record["import_status"] = detail.import_status;
    import_record["importer_name"] =
        detail.importer_name.has_value() ? Json::Value(*detail.importer_name) : Json::Value(Json::nullValue);
    import_record["importer_version"] =
        detail.importer_version.has_value() ? Json::Value(*detail.importer_version) : Json::Value(Json::nullValue);
    import_record["created_at"] = detail.created_at;
    import_record["updated_at"] = detail.updated_at;

    Json::Value bridge;
    bridge["id"] = detail.bridge_id;
    bridge["system_number"] = detail.bridge_system_number;
    bridge["bridge_name"] = detail.bridge_name;
    bridge["route_name"] =
        detail.bridge_route_name.has_value() ? Json::Value(*detail.bridge_route_name) : Json::Value(Json::nullValue);

    Json::Value inspection_year(Json::nullValue);
    if (detail.inspection_year_id.has_value()) {
        inspection_year = Json::Value(Json::objectValue);
        inspection_year["id"] = *detail.inspection_year_id;
        inspection_year["system_number"] =
            detail.inspection_year_system_number.has_value() ? *detail.inspection_year_system_number : "";
        inspection_year["inspection_year"] =
            detail.inspection_year.has_value() ? *detail.inspection_year : 0;
        inspection_year["status"] =
            detail.inspection_year_status.has_value() ? *detail.inspection_year_status : "";
        inspection_year["version_number"] =
            detail.inspection_year_version_number.has_value() ? *detail.inspection_year_version_number : 0;
        inspection_year["is_current"] =
            detail.inspection_year_is_current.has_value() && *detail.inspection_year_is_current;
    }

    Json::Value body;
    body["import_record"] = import_record;
    body["bridge"] = bridge;
    body["inspection_year"] = inspection_year;
    body["parsed_result"] = parsed_result;
    body["statistics"] = statistics.to_json();
    body["has_current_annual_facts"] = has_current_annual_facts;
    return body;
}

}  // 命名空间 bridge_report::review
