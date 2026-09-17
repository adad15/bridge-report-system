#include "bridge_report/report/ReportContextModels.hpp"

namespace bridge_report::report {
namespace {

void put(Json::Value& json, const char* key, const std::optional<std::string>& value) {
    json[key] = value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

void put(Json::Value& json, const char* key, const std::optional<int>& value) {
    json[key] = value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

void put(Json::Value& json, const char* key, const std::optional<double>& value) {
    json[key] = value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

}  // namespace

std::string display_component_name(const std::string& name) {
    // 全角括号在 UTF-8 里各占三个字节；按字节找子串即可，不必逐码点走。
    static const std::string open = "（";
    static const std::string close = "）";

    std::string result = name;
    for (;;) {
        const auto start = result.find(open);
        if (start == std::string::npos) break;
        const auto end = result.find(close, start + open.size());
        // 括号不成对说明名称本身就长这样，原样留着，不猜到哪儿截。
        if (end == std::string::npos) break;
        result.erase(start, end + close.size() - start);
    }

    const auto first = result.find_first_not_of(" \t");
    const auto last = result.find_last_not_of(" \t");
    if (first == std::string::npos) return name;
    result = result.substr(first, last - first + 1);
    // 整个名称都在括号里时宁可印全称，也不印一个空格。
    return result.empty() ? name : result;
}

std::string ReportPhoto::caption() const {
    // 与两份正式报告一致：「照片2.1-1␠␠1-1#板横向裂缝」，两个空格。
    // 标题为空时只输出图号，不得用病害描述臆造标题（设计 §11.7）。
    if (!title.has_value() || title->empty()) return report_number;
    return report_number + "  " + *title;
}

Json::Value ReportPhoto::to_json() const {
    Json::Value json;
    json["photo_id"] = photo_id;
    json["archived_file_id"] = archived_file_id;
    json["storage_relative_path"] = storage_relative_path;
    json["report_number"] = report_number;
    put(json, "title", title);
    json["caption"] = caption();
    put(json, "source_photo_number", source_photo_number);
    return json;
}

Json::Value ReportDefectRow::to_json() const {
    Json::Value json;
    json["row_number"] = row_number;
    json["observation_id"] = observation_id;
    put(json, "part_name", part_name);
    put(json, "component_number", component_number);
    put(json, "defect_location", defect_location);
    json["defect_type"] = defect_type;
    json["description"] = description;
    put(json, "scale", scale);
    put(json, "deduction", deduction);
    put(json, "component_score", component_score);
    json["photo_numbers"] = Json::Value(Json::arrayValue);
    for (const auto& number : photo_numbers) json["photo_numbers"].append(number);
    return json;
}

Json::Value PartComparison::to_json() const {
    Json::Value json;
    json["current_source_defect_count"] = current_source_defect_count;
    json["previous_source_defect_count"] = previous_source_defect_count;
    json["delta"] = delta;
    json["has_previous"] = has_previous;
    return json;
}

Json::Value ReportStructurePart::to_json() const {
    Json::Value json;
    json["part_code"] = part_code;
    json["part_label"] = part_label;
    json["defect_rows"] = Json::Value(Json::arrayValue);
    for (const auto& row : defect_rows) json["defect_rows"].append(row.to_json());
    json["photos"] = Json::Value(Json::arrayValue);
    for (const auto& photo : photos) json["photos"].append(photo.to_json());
    json["comparison"] = comparison.to_json();
    return json;
}

Json::Value ReportAssessmentPart::to_json() const {
    Json::Value json;
    json["part_code"] = part_code;
    json["part_label"] = part_label;
    json["score"] = score;
    put(json, "grade", grade);
    put(json, "weight", weight);
    return json;
}

Json::Value ReportScoreBand::to_json() const {
    Json::Value json;
    json["score"] = score;
    json["component_count"] = component_count;
    return json;
}

Json::Value ReportAssessmentCategory::to_json() const {
    Json::Value json;
    json["part_code"] = part_code;
    json["part_label"] = part_label;
    json["category_id"] = category_id;
    put(json, "category_name", category_name);
    json["component_count"] = component_count;
    json["score"] = score;
    put(json, "grade", grade);
    put(json, "weight", weight);
    json["score_bands"] = Json::Value(Json::arrayValue);
    for (const auto& band : score_bands) json["score_bands"].append(band.to_json());
    return json;
}

Json::Value ReportComponentWeight::to_json() const {
    Json::Value json;
    json["part_code"] = part_code;
    json["part_label"] = part_label;
    json["order"] = order;
    json["category_id"] = category_id;
    put(json, "category_name", category_name);
    json["configured_weight"] = configured_weight;
    put(json, "effective_weight", effective_weight);
    put(json, "component_count", component_count);
    json["present"] = present;
    return json;
}

Json::Value ReportControlIndicator::to_json() const {
    Json::Value json;
    json["rule_id"] = rule_id;
    json["message"] = message;
    put(json, "grade_after", grade_after);
    return json;
}

Json::Value ReportTopDeduction::to_json() const {
    Json::Value json;
    json["part_code"] = part_code;
    put(json, "component_number", component_number);
    json["defect_type"] = defect_type;
    json["deduction"] = deduction;
    return json;
}

Json::Value ReportAssessment::to_json() const {
    Json::Value json;
    json["has_formal_run"] = has_formal_run;
    put(json, "overall_score", overall_score);
    put(json, "overall_grade", overall_grade);
    json["parts"] = Json::Value(Json::arrayValue);
    for (const auto& part : parts) json["parts"].append(part.to_json());
    json["categories"] = Json::Value(Json::arrayValue);
    for (const auto& item : categories) json["categories"].append(item.to_json());
    json["top_deductions"] = Json::Value(Json::arrayValue);
    for (const auto& item : top_deductions) json["top_deductions"].append(item.to_json());
    json["component_weights"] = Json::Value(Json::arrayValue);
    for (const auto& item : component_weights) {
        json["component_weights"].append(item.to_json());
    }
    json["triggered_controls"] = Json::Value(Json::arrayValue);
    for (const auto& item : triggered_controls) {
        json["triggered_controls"].append(item.to_json());
    }
    return json;
}

Json::Value ReportBridgeProfile::to_json() const {
    Json::Value json;
    put(json, "business_code", business_code);
    put(json, "station_mark", station_mark);
    put(json, "bridge_type", bridge_type);
    put(json, "bridge_scale", bridge_scale);
    put(json, "span_combination", span_combination);
    put(json, "bridge_length_m", bridge_length_m);
    put(json, "bridge_width_m", bridge_width_m);
    put(json, "built_year", built_year);
    put(json, "maintenance_org", maintenance_org);
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
    put(json, "supervision_org", supervision_org);
    return json;
}

Json::Value ReportPersonnelEntry::to_json() const {
    Json::Value json;
    json["full_name"] = full_name;
    put(json, "organization", organization);
    put(json, "professional_title", professional_title);
    put(json, "qualification_certificate_no", qualification_certificate_no);
    json["role_code"] = role_code;
    return json;
}

Json::Value ReportEquipmentEntry::to_json() const {
    Json::Value json;
    json["equipment_name"] = equipment_name;
    put(json, "model_spec", model_spec);
    put(json, "asset_number", asset_number);
    put(json, "measurement_range", measurement_range);
    put(json, "accuracy", accuracy);
    put(json, "calibration_certificate_no", calibration_certificate_no);
    put(json, "calibration_valid_until", calibration_valid_until);
    put(json, "purpose", purpose);
    return json;
}

Json::Value ReportContext::to_json() const {
    Json::Value json;
    json["inspection_year_id"] = inspection_year_id;
    json["template_id"] = template_id;
    json["template_code"] = template_code;
    json["template_config"] = template_config;

    // 标量占位符的取值单独成块：Docx Builder 直接按名字取，不用再理解上下文结构。
    Json::Value scalars;
    scalars["report_no"] = report_no;
    scalars["bridge_name"] = bridge_name;
    put(scalars, "route_code", route_code);
    put(scalars, "route_name", route_name);
    put(scalars, "administrative_region", administrative_region);
    put(scalars, "inspection_date", inspection_date);
    scalars["inspection_year"] = std::to_string(inspection_year);
    put(scalars, "project_name", project_name);
    put(scalars, "inspection_org", inspection_org);
    scalars["report_date"] = report_date;
    put(scalars, "overall_grade", overall_grade);
    scalars["comparison_year"] = comparison_year.has_value()
        ? Json::Value(std::to_string(*comparison_year))
        : Json::Value(Json::nullValue);
    json["scalars"] = scalars;

    json["parts"] = Json::Value(Json::arrayValue);
    for (const auto& part : parts) json["parts"].append(part.to_json());
    json["personnel"] = Json::Value(Json::arrayValue);
    for (const auto& item : personnel) json["personnel"].append(item.to_json());
    json["equipment"] = Json::Value(Json::arrayValue);
    for (const auto& item : equipment) json["equipment"].append(item.to_json());
    json["assessment"] = assessment.to_json();
    json["bridge_profile"] = bridge_profile.to_json();
    json["bridge_media"] = Json::Value(Json::arrayValue);
    for (const auto& item : bridge_media) json["bridge_media"].append(item.to_json());
    json["overall_comparison"] = overall_comparison.to_json();
    return json;
}

Json::Value ReportBridgeMedia::to_json() const {
    Json::Value json;
    json["slot"] = slot;
    json["storage_relative_path"] = storage_relative_path;
    return json;
}

}  // namespace bridge_report::report
