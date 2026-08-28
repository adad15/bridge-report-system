#pragma once

#include <memory>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

// 可确认病害视图（设计 §17）。
//
// 预检和正式确认不再从病害 JSON 读构件或评分树解析字段，而是由这里把
//
//     来源病害事实
//   + 构件组及解析目标
//   + 病害解析实例及实例级覆盖
//   + 评分树解析
//   = 可确认病害视图
//
// 组合出来。视图**形状上仍是一份 BridgeAnnualInspectionData**：这样预检与写计划
// 那两大段成熟逻辑一行不用改，变的只是数据从哪儿来。
namespace bridge_report::resolution {

/**
 * @brief 把来源草稿与关系表里的解析状态组合成可确认病害视图。
 *
 * `defects[]` 每一项对应**一条活动实例**，而不是一条来源病害：
 *   - `candidate_id` 是实例 id（计划内唯一，多实例不会互相覆盖）；
 *   - `source_candidate_id` 保留来源病害身份，供预检做来源级去重（§17.1）；
 *   - 病害事实取有效值（来源值叠加实例覆盖）；
 *   - 构件、类别、结构部位由目标在组所钉台账版本里的条目派生；
 *   - 评分树节点与标准指标取该实例的评分树解析；
 *   - 照片引用只留在 `is_photo_owner` 的实例上，其余写空数组；
 *   - 展开为多个活动实例时合成 `range_split_origin`（§17.2）。
 *
 * `photos[]` 的 `linked_defect_candidate_id` 一并改指到持有照片的那条实例——不改的话
 * 写计划会因为找不到对应病害而把照片整批丢掉。
 *
 * 未解析、已标记缺失或没有评分树解析的实例照常出现在视图里，但缺什么就缺什么，
 * 由预检按现有规则阻断；这里不替它们编造值。
 */
[[nodiscard]] Json::Value build_confirmable_view(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const std::string& import_record_id,
    const Json::Value& parsed_result);

/// 供不在事务里的只读调用方（预检接口）使用。
[[nodiscard]] Json::Value build_confirmable_view(
    const drogon::orm::DbClientPtr& client,
    const std::string& import_record_id,
    const Json::Value& parsed_result);

}  // namespace bridge_report::resolution
