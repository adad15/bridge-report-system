#include "bridge_report/review/DraftValidation.hpp"

#include "bridge_report/contracts/AnnualInspectionContract.hpp"

namespace bridge_report::review {

DraftValidationResult validate_review_draft(
    const Json::Value& body,
    const std::string& record_system_number,
    const std::string& record_import_status
) {
    DraftValidationResult result;

    if (record_import_status != "待校对") {
        result.ok = false;
        result.code = "import_record_not_editable";
        result.message = "导入记录当前状态不是待校对，无法保存草稿。";
        return result;
    }

    const auto contract_result = contracts::validate_bridge_annual_inspection_data(body);
    if (!contract_result.ok()) {
        result.ok = false;
        result.code = "contract_validation_failed";
        result.message = "请求体未通过契约校验。";
        for (const auto& issue : contract_result.issues()) {
            result.issues.push_back(DraftValidationIssue{issue.path, issue.message});
        }
        return result;
    }

    const auto& import_context = body["import_context"];
    const auto body_system_number = import_context.isObject() && import_context.isMember("import_record_system_number")
        ? import_context["import_record_system_number"].asString()
        : std::string();
    if (body_system_number != record_system_number) {
        result.ok = false;
        result.code = "import_context_mismatch";
        result.message = "请求体的导入记录编号与目标记录不一致。";
        return result;
    }

    result.ok = true;
    return result;
}

}  // 命名空间 bridge_report::review
