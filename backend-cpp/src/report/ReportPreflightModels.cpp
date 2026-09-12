#include "bridge_report/report/ReportPreflightModels.hpp"

#include <algorithm>

namespace bridge_report::report {

std::optional<std::string> structure_part_code(const std::string& label) {
    for (const auto& part : kStructureParts) {
        if (label == part.label) return std::string(part.code);
    }
    return std::nullopt;
}

Json::Value PreflightFinding::to_json() const {
    Json::Value json;
    json["code"] = code;
    json["message"] = message;
    json["severity"] = severity == PreflightSeverity::Blocking ? "blocking" : "warning";
    return json;
}

bool ReportPreflightResult::can_generate() const {
    return std::none_of(findings.begin(), findings.end(), [](const PreflightFinding& finding) {
        return finding.severity == PreflightSeverity::Blocking;
    });
}

Json::Value ReportPreflightResult::to_json() const {
    Json::Value json;
    json["inspection_year_id"] = inspection_year_id;
    json["inspection_year"] = inspection_year;
    json["can_generate"] = can_generate();

    json["findings"] = Json::Value(Json::arrayValue);
    for (const auto& finding : findings) json["findings"].append(finding.to_json());

    Json::Value summary;
    summary["defect_component_count"] = defect_component_count;
    summary["defect_observation_count"] = defect_observation_count;
    // 两个数并列摆出来是有意的：差值就是构件范围拆分放大的倍数，报告里的对比用的是
    // 去重后的来源病害数（设计 §12.2）。生成页把两者都显示出来，避免用户误读。
    summary["source_defect_count"] = source_defect_count;
    summary["photo_count"] = photo_count;
    summary["overall_grade"] = overall_grade.has_value() ? Json::Value(*overall_grade)
                                                         : Json::Value(Json::nullValue);
    summary["structure_parts_with_data"] = Json::Value(Json::arrayValue);
    for (const auto& part : structure_parts_with_data) {
        summary["structure_parts_with_data"].append(part);
    }
    summary["template_covered_parts"] = Json::Value(Json::arrayValue);
    for (const auto& part : template_covered_parts) {
        summary["template_covered_parts"].append(part);
    }
    json["summary"] = summary;
    return json;
}

}  // namespace bridge_report::report
