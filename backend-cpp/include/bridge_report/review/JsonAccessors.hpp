#pragma once

#include <string>

#include <json/value.h>

namespace bridge_report::review {

/**
 * @brief 病害候选 review_status 的两个"已定案"取值。
 *
 * 与数据库 check 约束（defect_observations.review_status / condition_ratings.review_status）
 * 的合法取值子集一致：候选只有落在这两个状态时才会进入 ConfirmPlan / 通过入库前检查。
 */
inline constexpr const char* kConfirmed = "已确认";
inline constexpr const char* kModified = "已修改";

/**
 * @brief 判断候选 review_status 是否已定案（已确认或已修改）。
 */
inline bool is_review_settled(const std::string& status) {
    return status == kConfirmed || status == kModified;
}

/**
 * @brief 安全读取对象的字符串字段：非对象/缺字段/非字符串类型均返回空字符串，不抛异常。
 */
inline std::string string_member_or_empty(const Json::Value& object, const char* key) {
    if (!object.isObject() || !object.isMember(key) || !object[key].isString()) {
        return std::string();
    }
    return object[key].asString();
}

// candidate_id 是候选定位字段；review_status 只用于仍有人工校对状态的候选。
inline std::string candidate_id_of(const Json::Value& candidate) {
    return string_member_or_empty(candidate, "candidate_id");
}

inline std::string review_status_of(const Json::Value& candidate) {
    return string_member_or_empty(candidate, "review_status");
}

/**
 * @brief 该候选背后的来源病害身份。
 *
 * 可确认病害视图里一条来源病害会展开成多条实例，每条实例有自己的 candidate_id，
 * 但 review_status / group_review_status 这类校对事实是**来源病害**的属性。
 * 按实例逐条报，同一件事会重复 N 遍；用它做来源级去重。
 *
 * 视图之外（原始草稿）没有这个字段，此时来源身份就是候选自己。
 */
inline std::string source_candidate_id_of(const Json::Value& candidate) {
    const auto source = string_member_or_empty(candidate, "source_candidate_id");
    return source.empty() ? candidate_id_of(candidate) : source;
}

}  // 命名空间 bridge_report::review
