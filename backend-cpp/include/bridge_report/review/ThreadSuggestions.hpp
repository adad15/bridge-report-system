#pragma once

#include <string>
#include <vector>

#include <json/value.h>

namespace bridge_report::review {

/**
 * @brief 病害线索候选建议（模块 06 §6.4）。纯函数，不访问数据库，不持久化建议。
 *
 * 候选集必须已经限定为与观测同一构件的线索（跨构件绝不建议）。
 * 匹配依据：规范化后的相同病害类型、相同或相近详细位置；
 * 得分 = 类型匹配×2 + 位置全等×1 + 位置包含×0.5，仅返回得分 > 0 的线索。
 * 建议只是排序依据，绝不自动写 defect_thread_id；绑定只由人工发起。
 */
struct ThreadSuggestionInput {
    /// 判"是不是同一种病害"用它，与批量归组同一口径（ThreadCanonicalKey，迁移 029）。
    std::string node_key;
    /// 只作展示，不参与打分。
    std::string defect_type;
    std::string defect_location;
};

// 文本规范化：去除全部 ASCII 空白、常见全角标点转半角、ASCII 字母小写化。
[[nodiscard]] std::string normalize_suggestion_text(const std::string& input);

// threads 为线索 JSON 数组（须含 node_key/defect_type/defect_location/latest_seen_year 成员）。
// 返回按 得分降序 -> latest_seen_year 降序 -> system_number 升序 排序的建议数组，
// 每项在原线索 JSON 上附加 match_basis{same_component,same_defect_type,location_exact,
// location_contains} 与 suggestion_score。
[[nodiscard]] Json::Value suggest_threads(const ThreadSuggestionInput& observation, const Json::Value& threads);

}  // namespace bridge_report::review
