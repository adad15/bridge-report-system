#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/report/InspectionReportSettingsModels.hpp"

namespace bridge_report::db {

/**
 * @brief 年度报告配置的数据访问入口（设计 §15.3、§12.1）。
 *
 * 读与写不对称，这是有意的：
 *
 *  - 读会把停用的人员、设备和失效的模板照样带出来，并标记 needs_reconfirmation。
 *    历史配置要看得见，否则用户不知道自己之前选了谁（设计 §15.3）。
 *  - 写则拒绝停用项：新配置不能把一个已经停用的人静默选进去。
 *
 * 历史对比检查存在 inspection_years.report_comparison_inspection_id，不在配置表里
 * 再建一份同义字段（设计 §15.3）。候选条件在保存和生成时各校验一次（设计 §12.1）。
 */
class InspectionReportSettingsRepository {
public:
    explicit InspectionReportSettingsRepository(drogon::orm::DbClientPtr client);

    /// 读取某年度的当前配置。年度不存在时返回空。
    std::optional<report::InspectionReportSettings> find(
        const std::string& inspection_year_id) const;

    /// 该年度可选的历史对比检查（同桥、is_current、已确认或已归档、年度更早、非自身）。
    std::vector<report::ComparisonCandidate> list_comparison_candidates(
        const std::string& inspection_year_id) const;

    /// 整体替换该年度的报告配置。人员和设备是全量覆盖，不做增量合并。
    report::SettingsWriteStatus save(
        const std::string& inspection_year_id,
        const report::InspectionReportSettingsInput& input) const;

private:
    drogon::orm::DbClientPtr client_;
};

}  // namespace bridge_report::db
