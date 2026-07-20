#include "bridge_report/review/ReviewStatistics.hpp"

namespace bridge_report::review {

namespace {

constexpr const char* kPending = "待确认";
constexpr const char* kConfirmed = "已确认";
constexpr const char* kModified = "已修改";
constexpr const char* kIgnored = "已忽略";

bool has_non_empty_array_member(const Json::Value& object, const char* key) {
    return object.isObject() && object.isMember(key) && object[key].isArray() && !object[key].empty();
}

// 按 candidate 的 review_status 字段累加到对应统计计数器。
void tally_review_status(const Json::Value& candidate, ReviewStatistics& stats) {
    if (!candidate.isObject() || !candidate.isMember("review_status") || !candidate["review_status"].isString()) {
        return;
    }
    const auto status = candidate["review_status"].asString();
    if (status == kPending) {
        ++stats.pending_count;
    } else if (status == kConfirmed) {
        ++stats.confirmed_count;
    } else if (status == kModified) {
        ++stats.modified_count;
    } else if (status == kIgnored) {
        ++stats.ignored_count;
    }
}

void tally_object_warnings(const Json::Value& candidate, ReviewStatistics& stats) {
    if (has_non_empty_array_member(candidate, "warnings")) {
        ++stats.object_warning_count;
    }
}

}  // 匿名命名空间

Json::Value ReviewStatistics::to_json() const {
    Json::Value json;
    json["defect_count"] = defect_count;
    json["photo_count"] = photo_count;
    json["rating_item_count"] = rating_item_count;
    json["pending_count"] = pending_count;
    json["confirmed_count"] = confirmed_count;
    json["modified_count"] = modified_count;
    json["ignored_count"] = ignored_count;
    json["object_warning_count"] = object_warning_count;
    return json;
}

ReviewStatistics build_review_statistics(const Json::Value& parsed_result) {
    ReviewStatistics stats;

    if (!parsed_result.isObject()) {
        return stats;
    }

    if (parsed_result.isMember("defects") && parsed_result["defects"].isArray()) {
        const auto& defects = parsed_result["defects"];
        stats.defect_count = static_cast<int>(defects.size());
        for (const auto& defect : defects) {
            tally_review_status(defect, stats);
            tally_object_warnings(defect, stats);
        }
    }

    if (parsed_result.isMember("photos") && parsed_result["photos"].isArray()) {
        const auto& photos = parsed_result["photos"];
        stats.photo_count = static_cast<int>(photos.size());
        for (const auto& photo : photos) {
            tally_review_status(photo, stats);
            tally_object_warnings(photo, stats);
        }
    }

    return stats;
}

}  // 命名空间 bridge_report::review
