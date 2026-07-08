#pragma once

#include <string>
#include <vector>

#include <json/value.h>

namespace bridge_report::review {

/**
 * @brief 校对草稿保存前的问题条目，路径沿用契约校验的 JSONPath 风格。
 */
struct DraftValidationIssue {
    std::string path;
    std::string message;
};

/**
 * @brief PUT review-draft 请求体的校验结果。
 *
 * ok 为 true 时 code/message/issues 均为空。
 * ok 为 false 时 code 取值为：
 *   - import_record_not_editable：导入记录当前状态不是“待校对”
 *   - contract_validation_failed：请求体未通过 BridgeAnnualInspectionData 契约校验（issues 非空）
 *   - import_context_mismatch：请求体 import_context.import_record_system_number 与记录的 system_number 不一致
 */
struct DraftValidationResult {
    bool ok{false};
    std::string code;
    std::string message;
    std::vector<DraftValidationIssue> issues;
};

/**
 * @brief 纯函数：按顺序检查导入记录是否可编辑、请求体是否满足契约、导入上下文是否与记录一致。
 *
 * 检查顺序（返回第一类错误）：
 *   1. record_import_status 不是“待校对” -> import_record_not_editable
 *   2. body 未通过 validate_bridge_annual_inspection_data -> contract_validation_failed
 *   3. body.import_context.import_record_system_number != record_system_number -> import_context_mismatch
 */
[[nodiscard]] DraftValidationResult validate_review_draft(
    const Json::Value& body,
    const std::string& record_system_number,
    const std::string& record_import_status
);

}  // 命名空间 bridge_report::review
