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

/**
 * @brief 纯函数：草稿中是否存在带警告的病害候选（defects[i].warnings 为非空数组）。
 *
 * 重开校对 scope=warnings_only 的准入条件：没有警告病害就没有可修正对象。
 */
[[nodiscard]] bool draft_has_warning_defects(const Json::Value& data);

/**
 * @brief 纯函数：warnings_only 重开态的保存范围校验。
 *
 * 以库中已存草稿（重开快照后的当前草稿）为基准：
 *   1. 不允许新增或删除病害候选（candidate_id 集合必须一致）；
 *   2. 基准侧 warnings 为空的病害候选必须逐字段与新草稿完全一致；
 *   3. 带警告病害只能修改白名单业务字段；照片、来源证据和其他顶层数据必须不变；
 *   4. 扣分变化引起的构件评分只能等于后端按同一纯函数生成的派生结果。
 * "是否带警告"只看基准侧（存量 JSON），客户端无法通过在请求体里
 * 添改 warnings 数组把锁定病害伪装成可修改。
 *
 * 违规时 ok=false、code="reopen_scope_violation"，issues 指向具体病害候选。
 */
[[nodiscard]] DraftValidationResult validate_warnings_only_scope(
    const Json::Value& stored_draft,
    const Json::Value& new_draft,
    Json::Value* normalized_draft = nullptr
);

}  // 命名空间 bridge_report::review
