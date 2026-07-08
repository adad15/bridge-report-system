#pragma once

#include <string>

#include <json/value.h>

namespace bridge_report::review {

/**
 * @brief 候选（病害/照片/评分项）review_status 的两个"已定案"取值。
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

// candidate_id / review_status 是所有候选（病害/照片/评分项）共有的定位与状态字段。
inline std::string candidate_id_of(const Json::Value& candidate) {
    return string_member_or_empty(candidate, "candidate_id");
}

inline std::string review_status_of(const Json::Value& candidate) {
    return string_member_or_empty(candidate, "review_status");
}

}  // 命名空间 bridge_report::review
