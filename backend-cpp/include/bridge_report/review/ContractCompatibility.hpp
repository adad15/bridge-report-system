#pragma once

#include <string_view>

#include <json/value.h>

namespace bridge_report::review {

// Native20：原生 2.0 数据，可正常校对。
// LegacyPendingReparse：待校对的 1.0/1.1/1.2 旧草稿，只读展示，必须经
//   POST /api/import-records/{id}/parse-word 重新解析为 2.0 后再校对，
//   不允许在旧草稿上补造字段后继续确认（变更提案 001 §7）。
// LegacyReadOnly：旧版终态记录（已确认/已取消/已被修订），永久只读。
enum class ContractCompatibility {
    Native20,
    LegacyPendingReparse,
    LegacyReadOnly,
};

// Task 11—17 的显式过渡门。只允许读取旧合同，不允许保存或确认；Task 18 删除。
inline constexpr bool kLegacyAnnualInspection12ReadEnabled = true;

struct ContractCompatibilityResult {
    Json::Value data;
    ContractCompatibility compatibility{ContractCompatibility::Native20};
};

// 对 1.0/1.1 数据做仅内存的 1.2 展示规范化，
// 返回值绝不回写数据库；存量 JSON 保持原版本，直到重新解析或修订导入。
ContractCompatibilityResult normalize_review_contract(
    Json::Value data,
    std::string_view import_status
);

std::string_view contract_compatibility_name(ContractCompatibility value);

// 存量草稿是否必须重新解析：合同版本不是 2.0 时保存草稿必须被拒绝，
// 防止客户端伪造 2.0 请求体绕过重新解析要求。
bool stored_contract_requires_reparse(const Json::Value& stored_data);

}  // namespace bridge_report::review
