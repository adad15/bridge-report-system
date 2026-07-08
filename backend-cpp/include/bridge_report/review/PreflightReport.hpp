#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace bridge_report::review {

/**
 * @brief 入库前检查所需的导入记录 / 桥梁 / 年度上下文，均来自数据库，纯函数不负责查询。
 */
struct PreflightContext {
    std::string import_status;             // import_records.import_status
    std::string record_system_number;      // import_records.system_number
    std::string bridge_system_number;      // bridges.system_number
    std::optional<int> inspection_year;    // inspection_years.inspection_year（记录已挂年度时）
    bool has_current_annual_facts{false};
};

/**
 * @brief 单条阻断错误或警告。target_candidate_id 为空字符串表示无具体定位对象（to_json 输出 null）。
 */
struct PreflightIssue {
    std::string code;
    std::string message;
    std::string target_candidate_id;
};

/**
 * @brief 入库前检查结果。can_confirm 为 true 当且仅当 blocking_errors 为空。
 *
 * requires_revision_confirmation 独立于 can_confirm：即使需要修订确认，只要没有阻断错误，
 * can_confirm 仍为 true（由调用方结合 confirm_revision 请求参数决定是否真正允许入库）。
 */
struct PreflightReport {
    bool can_confirm{false};
    bool requires_revision_confirmation{false};
    std::vector<PreflightIssue> blocking_errors;
    std::vector<PreflightIssue> warnings;

    [[nodiscard]] Json::Value to_json() const;
};

/**
 * @brief 纯函数：根据候选 JSON（BridgeAnnualInspectionData）和上下文构造入库前检查报告。
 *
 * 检查顺序（详见模块 05 规格 §9.3 / §10.3）：
 *   1. import_record_wrong_status - context.import_status 不是“待校对”
 *   2. contract_validation_failed - 契约校验不通过
 *
 * 检查 1、2 总是执行；一旦契约校验失败，直接短路返回（不再评估依赖字段结构的检查 3-7 与警告，
 * 因为契约都不满足时无法安全定位病害/照片/评分字段）。契约通过后才继续：
 *   3. import_context_mismatch - 导入上下文与记录/桥梁/年度不一致
 *   4. candidate_pending_review - 仍有候选处于待确认状态
 *   5. defect_missing_required_field - 已确认/已修改病害缺核心字段
 *   6. photo_link_unresolved - 已确认/已修改照片的病害关联无法解析
 *   7. rating_overall_missing - 全桥评分缺总分或等级
 * 以及非阻断警告：defect_without_photo / unreferenced_photo_ignored /
 * measurement_unstructured_kept / rating_parts_incomplete。
 */
[[nodiscard]] PreflightReport build_preflight_report(const Json::Value& data, const PreflightContext& context);

}  // 命名空间 bridge_report::review
