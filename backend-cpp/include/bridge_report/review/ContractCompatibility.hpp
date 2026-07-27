#pragma once

#include <string_view>

#include <json/value.h>

namespace bridge_report::review {

enum class ContractCompatibility {
    Native30,
};

struct ContractCompatibilityResult {
    Json::Value data;
    ContractCompatibility compatibility{ContractCompatibility::Native30};
};

// 让构件匹配警告与当前绑定状态保持一致：已绑定/已标记缺失时清除，
// 未处理时按有无候选恢复一条 required/ambiguous 警告；其他警告不变。
void reconcile_defect_component_match_warning(Json::Value& defect);
void reconcile_component_match_warnings(Json::Value& data);

// 运行时只接受原生 3.0；本函数不补造或展示旧合同结构。
ContractCompatibilityResult normalize_review_contract(
    Json::Value data,
    std::string_view import_status
);

std::string_view contract_compatibility_name(ContractCompatibility value);

// 存量草稿是否必须重新解析：合同版本不是 3.0 时保存草稿必须被拒绝。
bool stored_contract_requires_reparse(const Json::Value& stored_data);

}  // namespace bridge_report::review
