#include "bridge_report/report/InspectionReportSettingsModels.hpp"

namespace bridge_report::report {
namespace {

void put(Json::Value& json, const char* key, const std::optional<std::string>& value) {
    json[key] = value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

}  // namespace

Json::Value PersonnelAssignment::to_json() const {
    Json::Value json;
    json["personnel_id"] = personnel_id;
    json["full_name"] = full_name;
    put(json, "organization", organization);
    put(json, "professional_title", professional_title);
    json["role_code"] = role_code;
    json["sort_order"] = sort_order;
    json["is_enabled"] = is_enabled;
    // 停用的人还挂在配置上，生成前必须重新确认或替换（设计 §15.3）。
    json["needs_reconfirmation"] = !is_enabled;
    return json;
}

Json::Value EquipmentAssignment::to_json() const {
    Json::Value json;
    json["equipment_id"] = equipment_id;
    json["equipment_name"] = equipment_name;
    put(json, "model_spec", model_spec);
    put(json, "purpose", purpose);
    json["sort_order"] = sort_order;
    json["is_enabled"] = is_enabled;
    json["calibration_expired"] = calibration_expired;
    json["needs_reconfirmation"] = !is_enabled;
    return json;
}

Json::Value ComparisonCandidate::to_json() const {
    Json::Value json;
    json["inspection_year_id"] = inspection_year_id;
    json["inspection_year"] = inspection_year;
    json["status"] = status;
    put(json, "report_number", report_number);
    put(json, "overall_grade", overall_grade);
    return json;
}

Json::Value InspectionReportSettings::to_json() const {
    Json::Value json;
    json["inspection_year_id"] = inspection_year_id;
    json["inspection_year"] = inspection_year;
    put(json, "template_id", template_id);
    put(json, "template_name", template_name);
    json["template_is_usable"] = template_is_usable;
    put(json, "comparison_inspection_id", comparison_inspection_id);
    json["comparison_year"] = comparison_year.has_value() ? Json::Value(*comparison_year)
                                                          : Json::Value(Json::nullValue);
    json["comparison_is_usable"] = comparison_is_usable;

    json["personnel"] = Json::Value(Json::arrayValue);
    for (const auto& item : personnel) json["personnel"].append(item.to_json());
    json["equipment"] = Json::Value(Json::arrayValue);
    for (const auto& item : equipment) json["equipment"].append(item.to_json());

    put(json, "configured_by_display_name", configured_by_display_name);
    put(json, "configured_at", configured_at);

    // 生成页要一眼看出还差什么，而不是等点了生成再被预检打回来（设计 §21.4）。
    Json::Value blocking(Json::arrayValue);
    if (!template_id.has_value()) {
        blocking.append("尚未选择报告模板。");
    } else if (!template_is_usable) {
        blocking.append("所选模板已停用或校验失效，请重新选择。");
    }
    if (comparison_inspection_id.has_value() && !comparison_is_usable) {
        blocking.append("所选历史对比检查已失效，请重新选择。");
    }
    for (const auto& item : personnel) {
        if (!item.is_enabled) {
            blocking.append("人员「" + item.full_name + "」已停用，请重新确认或替换。");
        }
    }
    for (const auto& item : equipment) {
        if (!item.is_enabled) {
            blocking.append("设备「" + item.equipment_name + "」已停用，请重新确认或替换。");
        }
    }
    json["blocking_notes"] = blocking;
    return json;
}

}  // namespace bridge_report::report
