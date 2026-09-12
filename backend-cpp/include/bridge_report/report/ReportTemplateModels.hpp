#pragma once

#include <optional>
#include <string>

#include <json/value.h>

namespace bridge_report::report {

/// 一套可选的报告模板（设计 §7.1）。只保存当前文件，不维护修订历史。
struct ReportTemplate {
    std::string id;
    std::string template_code;
    std::string template_name;
    std::optional<std::string> description;
    std::string contract_type;
    std::string file_id;
    std::string file_checksum;
    std::string file_name;
    /// 编号格式与所需人员角色，即 template.json 的内容。
    Json::Value contract_config;
    std::string validation_status;
    /// 最近一次校验明细：问题列表、锚点文档顺序、用到的占位符和域。
    Json::Value validation_result;
    bool is_enabled{false};
    bool is_default{false};
    std::optional<std::string> updated_by_display_name;
    std::string updated_at;
    /// 被年度报告配置引用的次数。大于 0 时不能删除，只能停用（设计 §17.4）。
    int usage_count{0};

    Json::Value to_json() const;
};

/// 模板的元数据入参。文件本身单独走 TemplateFileInput。
struct ReportTemplateInput {
    std::string template_code;
    std::string template_name;
    std::optional<std::string> description;
    std::string contract_type;
    Json::Value contract_config;
    std::string updated_by_user_id;
};

/// 已经落盘、待登记的模板文件。
struct TemplateFileInput {
    std::string original_file_name;
    /// 相对 archive_root 的安全相对路径。
    std::string storage_relative_path;
    /// 形如 sha256:<64 hex>。
    std::string checksum;
    long long size_bytes{0};
};

/// Python 校验的结论。校验不通过不是错误，是一种正常结果（设计 §23.1）。
struct TemplateValidationOutcome {
    bool is_valid{false};
    Json::Value result;
};

enum class TemplateWriteStatus {
    Ok,
    NotFound,
    /// template_code 已被别的模板占用。
    DuplicateCode,
    /// 校验未通过的模板不许启用，也不许设为默认（设计 §7.1）。
    NotValidated,
    /// 已被年度报告配置引用，不能删除（设计 §17.4）。
    Referenced,
    /// 当前的默认模板不能直接停用或删除：先把默认让给别的模板，
    /// 否则普通用户会选不到任何模板（设计 §7.1）。
    IsDefaultTemplate,
};

/// 替换模板文件后，旧文件是否还被别的业务引用；不再被引用的才允许清理磁盘。
struct TemplateFileReplacement {
    TemplateWriteStatus status{TemplateWriteStatus::NotFound};
    /// 旧文件的存储相对路径；仅当它已经无人引用时才有值。
    std::optional<std::string> obsolete_storage_relative_path;
};

}  // namespace bridge_report::report
