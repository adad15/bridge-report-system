#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

namespace bridge_report::report {

/// 生成任务的状态机（设计 §17.2）。
///
/// 处理中的任意状态都可以进入 Failed；Failed 任务不提供下载。Ready 到期后转 Expired，
/// 同时删掉临时文件——系统不保存报告版本，过期后只能重新生成。
enum class JobStatus {
    Queued,
    ValidatingData,
    AssemblingDocx,
    UpdatingFields,
    ValidatingDocx,
    Ready,
    Failed,
    Expired,
};

/// 数据库里的字面值。状态名是接口契约，前端按它显示阶段，不能随手改。
std::string job_status_text(JobStatus status);
std::optional<JobStatus> job_status_from_text(const std::string& text);

/// 处理中的状态。这些状态会阻断年度和桥梁删除（设计 §17.4）。
bool job_is_running(JobStatus status);

/// 终态。到了终态才允许对同一年度再起一个任务。
bool job_is_final(JobStatus status);

/// 一次生成任务。
///
/// 这是运行设施，不是报告版本表：文件路径只在有效期内有意义，过期后连同诊断正文
/// 一起清空（设计 §17.1）。
struct ReportGenerationJob {
    std::string id;
    std::string inspection_year_id;
    std::string requested_by_user_id;
    JobStatus status{JobStatus::Queued};
    /// 阶段进度与耗时，供界面显示"正在刷域"这类信息。不放业务正文。
    Json::Value progress{Json::objectValue};
    std::optional<std::string> template_id;
    std::optional<std::string> template_checksum;
    /// 成品的绝对路径。只有 Ready 才有值。
    std::optional<std::string> temporary_file_path;
    std::optional<std::string> error_code;
    std::optional<std::string> error_message;
    std::string created_at;
    std::optional<std::string> finished_at;
    std::optional<std::string> expires_at;

    /// 交付给用户的文件名（设计 §20）。生成那一刻拼定并存住：报告编号、路线名这些
    /// 字段之后可能被改，而已经生成的那份文件不该跟着改名。
    std::optional<std::string> download_filename;

    bool can_download() const;
    Json::Value to_json() const;
};

/// 成品文件名：`{报告编号}{行政区}{路线编号}{路线名称}{桥梁名称}定期检测报告（{综合等级}）.docx`
///
/// 缺失的片段直接省略，不留 null、连续占位符或多余空格。只在文件名层面清理 Windows
/// 非法字符，不修改文档内部的业务值（设计 §20）。
struct ReportFilenameParts {
    std::optional<std::string> report_number;
    std::optional<std::string> administrative_region;
    std::optional<std::string> route_code;
    std::optional<std::string> route_name;
    std::optional<std::string> bridge_name;
    std::optional<std::string> overall_grade;
};

std::string build_report_filename(const ReportFilenameParts& parts);

}  // namespace bridge_report::report
