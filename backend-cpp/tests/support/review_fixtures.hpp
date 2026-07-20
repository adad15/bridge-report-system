#pragma once

#include <json/value.h>

namespace bridge_report::test_support {

/**
 * @brief 把 BridgeAnnualInspectionData 2.0 中的病害、照片候选改为“已确认”，
 * 照片额外把 match_status 置为“已确认”，作为标准入库样本基底。
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
}

}  // 命名空间 bridge_report::test_support
