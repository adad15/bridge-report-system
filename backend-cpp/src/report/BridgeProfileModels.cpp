#include "bridge_report/report/BridgeProfileModels.hpp"

namespace bridge_report::report {
namespace {

void put(Json::Value& json, const char* key, const std::optional<std::string>& value) {
    json[key] = value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

void put(Json::Value& json, const char* key, const std::optional<double>& value) {
    json[key] = value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

void put(Json::Value& json, const char* key, const std::optional<int>& value) {
    json[key] = value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

}  // namespace

Json::Value BridgeProfile::to_json() const {
    Json::Value json;
    json["bridge_id"] = bridge_id;
    json["bridge_name"] = bridge_name;
    put(json, "business_code", business_code);
    put(json, "route_number", route_number);
    put(json, "route_name", route_name);
    put(json, "administrative_region", administrative_region);
    put(json, "station_mark", station_mark);
    put(json, "longitude", longitude);
    put(json, "latitude", latitude);

    put(json, "bridge_type", bridge_type);
    put(json, "bridge_scale", bridge_scale);
    put(json, "span_combination", span_combination);
    put(json, "bridge_length_m", bridge_length_m);
    put(json, "bridge_width_m", bridge_width_m);
    put(json, "built_year", built_year);

    put(json, "skew_angle_deg", skew_angle_deg);
    put(json, "carriageway_width_m", carriageway_width_m);
    put(json, "sidewalk_width_m", sidewalk_width_m);

    put(json, "deck_pavement", deck_pavement);
    put(json, "expansion_joint_type", expansion_joint_type);
    put(json, "expansion_joint_piers", expansion_joint_piers);
    put(json, "bearing_type", bearing_type);

    put(json, "superstructure_form", superstructure_form);
    put(json, "girders_per_span", girders_per_span);
    put(json, "girder_height_m", girder_height_m);

    put(json, "abutment_form", abutment_form);
    put(json, "pier_form", pier_form);
    put(json, "foundation_form", foundation_form);

    put(json, "design_load", design_load);
    put(json, "design_org", design_org);
    put(json, "construction_org", construction_org);
    put(json, "maintenance_org", maintenance_org);
    put(json, "supervision_org", supervision_org);
    return json;
}

}  // namespace bridge_report::report
