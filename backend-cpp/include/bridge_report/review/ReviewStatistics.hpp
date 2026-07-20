#pragma once

#include <json/value.h>

namespace bridge_report::review {

/**
 * @brief 校对数据统计，用于 GET /api/import-records/{import_record_id}/review 响应体的 statistics 字段。
 */
struct ReviewStatistics {
    int defect_count{0};
    int photo_count{0};
    int rating_item_count{0};
    int pending_count{0};
    int confirmed_count{0};
    int modified_count{0};
    int ignored_count{0};
    int object_warning_count{0};

    Json::Value to_json() const;
};

/**
 * @brief 根据解析结果 JSON（parsed_result）构造校对统计。纯函数，不访问数据库。
 *
 * - defect_count / photo_count：defects / photos 数组长度
 * - rating_item_count：固定为 0；系统评分由独立评定接口返回，不属于导入候选
 * - pending/confirmed/modified/ignored_count：defects + photos
 *   三层候选的 review_status 汇总（待确认/已确认/已修改/已忽略）
 * - object_warning_count：defects 和 photos 中对象级 warnings[] 非空的候选数
 *
 * parsed_result 为空对象 `{}` 或缺键时，全部字段为 0。
 */
ReviewStatistics build_review_statistics(const Json::Value& parsed_result);

}  // 命名空间 bridge_report::review
