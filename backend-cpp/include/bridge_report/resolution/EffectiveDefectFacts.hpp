#pragma once

#include <string>
#include <vector>

#include <json/value.h>

// 有效病害事实：来源病害事实叠加实例覆盖后的结果（设计 §8.4）。
//
// 覆盖一旦存在，"这条病害的类型是什么"就有两个答案。下游有四个消费方——哈希计算、
// 预检、正式确认、评定输入——任何一处直接读来源值，症状都是"界面上改过的实例，
// 算分/入库时用的还是老值"，且只在带覆盖的实例上出现。所以合并只有这一个出处。
namespace bridge_report::resolution {

/// 允许被实例覆盖的病害事实字段（§8.4 白名单）。
[[nodiscard]] const std::vector<std::string>& overridable_fact_fields();

/// 覆盖 JSON 是否合法：字段名、类型、可空性都要过。与 027 迁移里的 CHECK 同规则。
/// 不合法时 reason 给出可直接回给客户端的原因。
[[nodiscard]] bool fact_overrides_are_valid(
    const Json::Value& overrides, std::string& reason);

/**
 * @brief 把来源病害与实例覆盖合并成有效病害事实。
 *
 * 键存在即生效，包括显式的 null（仅白名单里的可空字段允许）。删除某个键即恢复
 * 来源值——这是唯一的"撤销覆盖"方式，接口层的"清除"就是删键，不是写 null。
 */
[[nodiscard]] Json::Value merge_effective_defect_facts(
    const Json::Value& source_defect, const Json::Value& fact_overrides);

/// 该实例实际脱离来源值的字段，按白名单顺序返回；供读模型标出"已按本实例单独设定"。
[[nodiscard]] std::vector<std::string> overridden_fact_fields(
    const Json::Value& fact_overrides);

}  // namespace bridge_report::resolution
