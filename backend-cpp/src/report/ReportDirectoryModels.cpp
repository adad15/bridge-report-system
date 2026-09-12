#include "bridge_report/report/ReportDirectoryModels.hpp"

namespace bridge_report::report {
namespace {

/// 可空字段统一输出 JSON null，而不是省略键——前端表单要能区分"没填"和"字段不存在"。
void put(Json::Value& json, const char* key, const std::optional<std::string>& value) {
    if (value.has_value()) {
        json[key] = *value;
    } else {
        json[key] = Json::Value(Json::nullValue);
    }
}

}  // namespace

Json::Value ReportPersonnel::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["full_name"] = full_name;
    put(json, "organization", organization);
    put(json, "job_title", job_title);
    put(json, "professional_title", professional_title);
    put(json, "qualification_certificate_no", qualification_certificate_no);
    put(json, "phone", phone);
    put(json, "email", email);
    put(json, "remarks", remarks);
    json["is_enabled"] = is_enabled;
    json["assignment_count"] = assignment_count;
    // 界面据此决定"删除"给不给：被引用的只能停用（设计 §15.3）。
    json["can_delete"] = assignment_count == 0;
    json["updated_at"] = updated_at;
    return json;
}

Json::Value ReportEquipment::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["equipment_name"] = equipment_name;
    put(json, "model_spec", model_spec);
    put(json, "asset_number", asset_number);
    put(json, "measurement_range", measurement_range);
    put(json, "accuracy", accuracy);
    put(json, "calibration_certificate_no", calibration_certificate_no);
    put(json, "calibration_valid_until", calibration_valid_until);
    put(json, "remarks", remarks);
    json["is_enabled"] = is_enabled;
    json["assignment_count"] = assignment_count;
    json["can_delete"] = assignment_count == 0;
    json["updated_at"] = updated_at;
    return json;
}

}  // namespace bridge_report::report
