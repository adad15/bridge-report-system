#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <json/value.h>

#include "bridge_report/inventory/ComponentInventoryModels.hpp"
#include "bridge_report/rating_tree/RatingTreeModels.hpp"
#include "bridge_report/rating_tree/RatingTreeResolver.hpp"

namespace bridge_report::review {

/// 一条病害的匹配结果摘要。候选、原因码和原因说明都是临时计算结果，
/// 不进入正式病害事实表，页面刷新后按同一草稿与同一规则重新获取。
struct DefectMatchRecord {
    std::string candidate_id;
    rating_tree::RatingTreeMatchOutcome outcome{
        rating_tree::RatingTreeMatchOutcome::unmatched};
    std::optional<std::string> node_id;  // 仅 auto_bound 有值
    std::string match_method;
    std::string match_evidence;
    std::vector<rating_tree::RatingTreeMatchCandidate> candidates;
    std::string reason_code;
    std::string reason_message;
    bool skipped{false};  // 人工选择 / 已确认 / 已忽略，自动结果不得覆盖
};

struct DefectMatchStats {
    int processed{0};
    int auto_bound{0};
    int candidates{0};
    int composite{0};
    int unmatched{0};
    int prerequisite_missing{0};
    int failed{0};
    int skipped{0};
};

struct DefectMatchReport {
    DefectMatchStats stats;
    std::vector<DefectMatchRecord> records;
};

/// 批量匹配作用域。candidate_ids 为空表示"本次导入的全部病害"。
struct DefectMatchScope {
    std::set<std::string> candidate_ids;
    bool has_scope{false};
};

/// 判断某条病害是否受人工/已确认保护，自动匹配不得覆盖。
[[nodiscard]] bool defect_is_protected_from_auto_match(const Json::Value& defect);

/**
 * 统一的批量匹配入口：导入、构件绑定、评定树绑定、草稿保存和页面"重新匹配"
 * 共用这一份规则，浏览器端不复制任何匹配逻辑。
 *
 * apply 为 true 时把 auto_bound 结果写回 draft 的 rating_tree_* 字段；
 * 为 false 时只计算并返回结果摘要（页面预览用），draft 不被修改。
 *
 * 相同输入、相同规则和相同评定树版本必须得到相同输出，重复执行幂等，
 * 并且绝不新增或删除病害、照片和关联关系。
 */
[[nodiscard]] DefectMatchReport match_defect_rating_tree_nodes(
    Json::Value& draft,
    const std::string& rating_tree_version_id,
    const std::string& technical_standard_package_id,
    const rating_tree::EffectiveRatingTree& tree,
    const std::optional<inventory::InventoryRevision>& resolved_revision,
    const DefectMatchScope& scope,
    bool apply);

/// 解析一条病害的桥型 + 规范构件类别；返回 nullopt 时给出具体原因码。
[[nodiscard]] std::optional<rating_tree::RatingTreeMatchInput>
build_defect_match_input(
    const Json::Value& defect,
    const std::string& technical_standard_package_id,
    const std::optional<inventory::InventoryRevision>& resolved_revision,
    std::string& reason_code,
    std::string& reason_message);

}  // namespace bridge_report::review
