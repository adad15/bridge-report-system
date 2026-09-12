#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace bridge_report::report {

/// 本年度分配的一位签字人员（设计 §15.3）。
struct PersonnelAssignment {
    std::string personnel_id;
    std::string full_name;
    std::optional<std::string> organization;
    std::optional<std::string> professional_title;
    std::string role_code;
    int sort_order{0};
    /// 人员库里是否仍启用。停用项仍要显示出来，但生成前必须由用户重新确认或替换
    /// （设计 §15.3）——静默沿用一个已经停用的人是最糟的结果。
    bool is_enabled{true};

    Json::Value to_json() const;
};

/// 本年度使用的一台检测设备。
struct EquipmentAssignment {
    std::string equipment_id;
    std::string equipment_name;
    std::optional<std::string> model_spec;
    std::optional<std::string> purpose;
    int sort_order{0};
    bool is_enabled{true};
    /// 检定有效期已过。不阻断保存，但生成页要显眼提示。
    bool calibration_expired{false};

    Json::Value to_json() const;
};

/// 可供选择的历史对比检查（设计 §12.1 的候选条件）。
struct ComparisonCandidate {
    std::string inspection_year_id;
    int inspection_year{0};
    std::string status;
    std::optional<std::string> report_number;
    std::optional<std::string> overall_grade;

    Json::Value to_json() const;
};

/// 一个年度检查的当前报告配置。
struct InspectionReportSettings {
    std::string inspection_year_id;
    int inspection_year{0};

    std::optional<std::string> template_id;
    std::optional<std::string> template_name;
    /// 所选模板是否仍然可用。停用或校验失效的模板要提示重新选择。
    bool template_is_usable{false};

    /// 历史对比检查，存在 inspection_years.report_comparison_inspection_id。
    std::optional<std::string> comparison_inspection_id;
    std::optional<int> comparison_year;
    /// 所选对比检查是否仍满足 §12.1 的条件（可能因修订或归档状态变化而失效）。
    bool comparison_is_usable{false};

    std::vector<PersonnelAssignment> personnel;
    std::vector<EquipmentAssignment> equipment;

    std::optional<std::string> configured_by_display_name;
    std::optional<std::string> configured_at;

    Json::Value to_json() const;
};

struct PersonnelAssignmentInput {
    std::string personnel_id;
    std::string role_code;
    int sort_order{0};
};

struct EquipmentAssignmentInput {
    std::string equipment_id;
    std::optional<std::string> purpose;
    int sort_order{0};
};

struct InspectionReportSettingsInput {
    std::optional<std::string> template_id;
    std::optional<std::string> comparison_inspection_id;
    std::vector<PersonnelAssignmentInput> personnel;
    std::vector<EquipmentAssignmentInput> equipment;
    std::string configured_by_user_id;
};

/// 保存结果。除 Ok 外都是能向用户解释清楚的具体原因，不是一句"保存失败"。
enum class SettingsWriteStatus {
    Ok,
    YearNotFound,
    TemplateNotFound,
    /// 只能选已启用且校验通过的模板。
    TemplateNotUsable,
    PersonnelNotFound,
    /// 停用的人员不能被新配置选中（设计 §15.3）。
    PersonnelDisabled,
    EquipmentNotFound,
    EquipmentDisabled,
    /// 对比检查不满足 §12.1 的候选条件。
    ComparisonInvalid,
    Failed,
};

}  // namespace bridge_report::report
