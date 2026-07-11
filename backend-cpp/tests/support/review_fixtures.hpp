#pragma once

#include <json/value.h>

namespace bridge_report::test_support {

/**
 * @brief 把样例 BridgeAnnualInspectionData JSON 中所有候选（defects/photos/ratings 三层）的
 * review_status 改为"已确认"（photos 额外把 match_status 也置为"已确认"），作为"标准入库样本"基底。
 *
 * 供 test_confirm_plan.cpp / test_preflight_report.cpp / test_review_repository.cpp 共用，
 * 避免三处各自维护一份逻辑相同的辅助函数。
 */
inline void confirm_all_candidates(Json::Value& data) {
    for (auto& defect : data["defects"]) {
        defect["review_status"] = "已确认";
        defect["group_review_status"] = "已确认";
    }
    for (auto& photo : data["photos"]) {
        photo["review_status"] = "已确认";
        photo["match_status"] = "已确认";
    }
    data["ratings"]["overall"]["review_status"] = "已确认";
    for (auto& part : data["ratings"]["structure_parts"]) {
        part["review_status"] = "已确认";
    }
    for (auto& part : data["ratings"]["evaluation_parts"]) {
        part["review_status"] = "已确认";
    }
}

}  // 命名空间 bridge_report::test_support
