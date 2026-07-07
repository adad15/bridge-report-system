#include "bridge_report/review/ReviewModels.hpp"

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

}  // 命名空间 bridge_report::review
