#include "bridge_report/report/ReportTemplateModels.hpp"

namespace bridge_report::report {

Json::Value ReportTemplate::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["template_code"] = template_code;
    json["template_name"] = template_name;
    json["description"] = description.has_value() ? Json::Value(*description)
                                                  : Json::Value(Json::nullValue);
    json["contract_type"] = contract_type;
    json["file_id"] = file_id;
    json["file_checksum"] = file_checksum;
    json["file_name"] = file_name;
    json["contract_config"] = contract_config;
    json["validation_status"] = validation_status;
    json["validation_result"] = validation_result;
    json["is_enabled"] = is_enabled;
    json["is_default"] = is_default;
    json["updated_by_display_name"] = updated_by_display_name.has_value()
        ? Json::Value(*updated_by_display_name)
        : Json::Value(Json::nullValue);
    json["updated_at"] = updated_at;
    json["usage_count"] = usage_count;
    // 界面据此决定"删除"给不给：被年度配置引用的只能停用（设计 §17.4）。
    json["can_delete"] = usage_count == 0;
    // 默认模板不能直接停用——先把默认让给别的模板，否则普通用户会选不到任何模板。
    json["can_disable"] = is_enabled && !is_default;
    return json;
}

}  // namespace bridge_report::report
