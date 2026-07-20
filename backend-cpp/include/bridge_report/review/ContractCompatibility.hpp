#pragma once

#include <string_view>

#include <json/value.h>

namespace bridge_report::review {

enum class ContractCompatibility {
    Native20,
};

struct ContractCompatibilityResult {
    Json::Value data;
    ContractCompatibility compatibility{ContractCompatibility::Native20};
};

// 运行时只接受原生 2.0；本函数不再补造或展示旧合同结构。
ContractCompatibilityResult normalize_review_contract(
    Json::Value data,
    std::string_view import_status
);

std::string_view contract_compatibility_name(ContractCompatibility value);

// 存量草稿是否必须重新解析：合同版本不是 2.0 时保存草稿必须被拒绝，
// 防止客户端伪造 2.0 请求体绕过重新解析要求。
bool stored_contract_requires_reparse(const Json::Value& stored_data);

}  // namespace bridge_report::review
