#pragma once

#include <optional>
#include <string>

#include <json/value.h>

namespace bridge_report::report {

/// 报告人员库中的一条记录（设计 §15.1）。
struct ReportPersonnel {
    std::string id;
    std::string full_name;
    std::optional<std::string> organization;
    std::optional<std::string> job_title;
    std::optional<std::string> professional_title;
    std::optional<std::string> qualification_certificate_no;
    std::optional<std::string> phone;
    std::optional<std::string> email;
    std::optional<std::string> remarks;
    bool is_enabled{true};
    /// 被年度报告配置引用的次数。大于 0 时只能停用，不能硬删除（设计 §15.3）。
    /// 界面要靠它决定"删除"按钮给不给，而不是让用户点下去撞外键。
    int assignment_count{0};
    std::string updated_at;

    Json::Value to_json() const;
};

/// 检测设备库中的一条记录（设计 §15.2）。
struct ReportEquipment {
    std::string id;
    std::string equipment_name;
    std::optional<std::string> model_spec;
    std::optional<std::string> asset_number;
    std::optional<std::string> measurement_range;
    std::optional<std::string> accuracy;
    std::optional<std::string> calibration_certificate_no;
    /// 检定或校准有效期，界面据此显示到期提醒（设计 §21.3）。
    std::optional<std::string> calibration_valid_until;
    std::optional<std::string> remarks;
    bool is_enabled{true};
    int assignment_count{0};
    std::string updated_at;

    Json::Value to_json() const;
};

/// 新建或更新时的入参。空白字符串一律归一化为空，避免"  "被当成有值。
struct ReportPersonnelInput {
    std::string full_name;
    std::optional<std::string> organization;
    std::optional<std::string> job_title;
    std::optional<std::string> professional_title;
    std::optional<std::string> qualification_certificate_no;
    std::optional<std::string> phone;
    std::optional<std::string> email;
    std::optional<std::string> remarks;
};

struct ReportEquipmentInput {
    std::string equipment_name;
    std::optional<std::string> model_spec;
    std::optional<std::string> asset_number;
    std::optional<std::string> measurement_range;
    std::optional<std::string> accuracy;
    std::optional<std::string> calibration_certificate_no;
    std::optional<std::string> calibration_valid_until;
    std::optional<std::string> remarks;
};

/// 删除结果。被引用是正常的业务结论，不是数据库故障。
enum class DirectoryDeleteStatus {
    Deleted,
    NotFound,
    /// 已被某个年度的报告配置引用，只能停用（设计 §15.3）。
    Referenced,
};

}  // namespace bridge_report::report
