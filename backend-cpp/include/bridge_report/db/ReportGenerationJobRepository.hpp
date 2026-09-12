#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/report/ReportGenerationJobModels.hpp"

namespace bridge_report::db {

/// 创建任务的结果（设计 §17.3 的幂等要求）。
struct JobCreation {
    enum class Outcome {
        /// 新建了一个任务。
        Created,
        /// 同一用户对同一年度已有进行中的任务，返回那一个，不再起第二个。
        AlreadyRunning,
        /// 年度不存在。
        YearNotFound,
    };

    Outcome outcome{Outcome::YearNotFound};
    std::optional<report::ReportGenerationJob> job;
};

/// 一次到期清理的结果，供日志和运行排障。
struct JobCleanupSummary {
    /// 到期转 expired 的任务数。
    int expired{0};
    /// 删掉的临时文件（目录）数。
    int files_removed{0};
    /// 超过保留期后物理删除的终态任务行数。
    int purged{0};
};

/**
 * @brief 报告生成任务的数据访问入口（设计 §17）。
 *
 * 这是运行设施，不是报告版本表。三条纪律：
 *
 *  - **同一用户同一年度只允许一个进行中的任务。** 靠数据库的部分唯一索引兜底，
 *    重复提交返回已有任务而不是再起一个——起第二个会让两份 Word 抢同一台机器。
 *  - **状态只能从进行中往前走，或者掉进 failed。** 终态不再变（除了 ready 到期转
 *    expired），否则一个迟到的回调能把已经失败的任务改成 ready。
 *  - **到期清空一切可能含业务正文的字段。** 系统不保存报告，过期后连诊断信息一起清。
 */
class ReportGenerationJobRepository {
public:
    explicit ReportGenerationJobRepository(drogon::orm::DbClientPtr client);

    /// 建一个 queued 任务；已有进行中的任务时原样返回它。
    JobCreation create(
        const std::string& inspection_year_id,
        const std::string& requested_by_user_id,
        const std::optional<std::string>& template_id,
        const std::optional<std::string>& template_checksum) const;

    std::optional<report::ReportGenerationJob> find(const std::string& job_id) const;

    /// 某年度最近的那个任务，也就是「当前报告」。
    ///
    /// 只取一条而不是列历史：系统不保存报告版本（§17.1），用户要的永远是「现在这份
    /// 能不能下」。终态行在库里还留 7 天，那是给排障用的，不是给界面翻的。
    std::optional<report::ReportGenerationJob> find_current(
        const std::string& inspection_year_id) const;

    /// 推进到下一个处理中状态。任务已经是终态时返回 false，不覆盖。
    bool advance(
        const std::string& job_id,
        report::JobStatus status,
        const Json::Value& progress) const;

    /// 标记成功：记录成品路径、下载文件名和到期时间。
    bool mark_ready(
        const std::string& job_id,
        const std::filesystem::path& output_path,
        const std::string& download_filename,
        int retention_hours,
        const Json::Value& progress) const;

    /// 标记失败。失败必须有错误码，否则界面只能显示"失败了"。
    bool mark_failed(
        const std::string& job_id,
        const std::string& error_code,
        const std::string& error_message) const;

    /// 到期清理：删文件、转 expired、清空路径与诊断正文，并物理删除过保留期的终态行。
    ///
    /// `job_root` 只用来校验待删路径确实落在受控临时根目录内——路径来自数据库，
    /// 删之前必须自证它在哪，不能拿到什么删什么。
    JobCleanupSummary cleanup(
        const std::filesystem::path& job_root, int history_days) const;

private:
    drogon::orm::DbClientPtr client_;
};

}  // namespace bridge_report::db
