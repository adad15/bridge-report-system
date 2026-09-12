#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/report/ReportTemplateModels.hpp"

namespace bridge_report::db {

/**
 * @brief 报告模板的数据访问入口（设计 §7.1、§17.4）。
 *
 * 三条规则住在这一层，不能交给调用方自觉遵守：
 *
 *  1. 校验未通过的模板不许启用，也不许设为默认；
 *  2. 同时只能有一个默认模板，切换默认必须与清除旧默认在同一事务里；
 *  3. 被年度报告配置引用的模板不许删除，只能停用。
 *
 * 模板文件登记进 archived_files（file_type='模板'），外键是 RESTRICT。替换文件时
 * 先原子更新模板对文件的引用，再把旧文件交还给调用方——只有确认无人引用的才允许
 * 从磁盘删掉（设计 §17.4）。仓储不碰文件系统。
 */
class ReportTemplateRepository {
public:
    explicit ReportTemplateRepository(drogon::orm::DbClientPtr client);

    std::vector<report::ReportTemplate> list(bool only_enabled) const;
    std::optional<report::ReportTemplate> find(const std::string& id) const;
    std::optional<report::ReportTemplate> find_default() const;

    /// 模板当前文件相对 archive_root 的路径。
    ///
    /// 不放进 ReportTemplate：那个结构会整个序列化给前端，服务器上的存储布局没有
    /// 必要出现在接口里。只有生成任务需要它（它要把模板复制到任务临时目录）。
    std::optional<std::string> find_file_relative_path(const std::string& id) const;

    /// 登记一套新模板。文件必须已经落盘。校验结论一并写入，不通过时模板保持停用。
    std::optional<report::ReportTemplate> create(
        const report::ReportTemplateInput& input,
        const report::TemplateFileInput& file,
        const report::TemplateValidationOutcome& validation,
        report::TemplateWriteStatus& status) const;

    /// 更新元数据与模板配置。配置变了校验结论也要跟着更新——编号格式是配置的一部分。
    std::optional<report::ReportTemplate> update(
        const std::string& id,
        const report::ReportTemplateInput& input,
        const report::TemplateValidationOutcome& validation,
        report::TemplateWriteStatus& status) const;

    /// 原子替换当前文件，返回旧文件中已无人引用、可以从磁盘清理的那一份。
    report::TemplateFileReplacement replace_file(
        const std::string& id,
        const report::TemplateFileInput& file,
        const report::TemplateValidationOutcome& validation,
        const std::string& updated_by_user_id) const;

    report::TemplateWriteStatus set_enabled(
        const std::string& id, bool enabled, const std::string& updated_by_user_id) const;

    /// 设为默认。同一事务内清掉旧默认，避免出现两个默认模板。
    report::TemplateWriteStatus set_default(
        const std::string& id, const std::string& updated_by_user_id) const;

    /// 删除模板；连同已无人引用的模板文件一起，把可清理的磁盘路径带出来。
    report::TemplateWriteStatus remove(
        const std::string& id,
        std::optional<std::string>& obsolete_storage_relative_path) const;

private:
    drogon::orm::DbClientPtr client_;
};

}  // namespace bridge_report::db
