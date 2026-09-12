#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <drogon/orm/DbClient.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/ReportGenerationJobRepository.hpp"
#include "bridge_report/report/ReportGenerationJobModels.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"

namespace bridge_report::report {

/// 稳定错误码（设计 §23.2）。界面按码给处理入口，不靠解析错误文字。
inline constexpr const char* kJobErrorPreflightBlocked = "report_preflight_blocked";
inline constexpr const char* kJobErrorTemplateMissing = "report_template_missing";
inline constexpr const char* kJobErrorTemplateFileMissing = "report_template_file_missing";
inline constexpr const char* kJobErrorContextUnavailable = "report_context_unavailable";
inline constexpr const char* kJobErrorAssembleFailed = "report_assemble_failed";
inline constexpr const char* kJobErrorFieldUpdateFailed = "report_field_update_failed";
inline constexpr const char* kJobErrorOutputInvalid = "report_output_invalid";
inline constexpr const char* kJobErrorToolsUnavailable = "report_tools_unavailable";
inline constexpr const char* kJobErrorInterrupted = "report_job_interrupted";
inline constexpr const char* kJobErrorUnexpected = "report_job_unexpected_error";

/**
 * @brief 生成任务的执行器：一条线程按 §17.2 的状态机跑完一次生成。
 *
 * 为什么是自己的线程而不是事件循环：一次生成要装配几百行病害表、把几百张照片重
 * 采样、再等 Word 刷完域，实测几十秒到几分钟。放在事件循环上会把整个 HTTP 服务
 * 卡住；而 Drogon 的同步 HTTP 客户端明确不能在它自己的循环线程里调。
 *
 * 为什么串行：刷域那一步在 Python 侧本来就是全局单并发（§17.3），并行装配只会让
 * 一堆任务同时堵在那道队列前面，还多占一份内存和磁盘。
 */
class ReportGenerationRunner {
public:
    ReportGenerationRunner(
        drogon::orm::DbClientPtr client,
        config::AppConfig config,
        std::shared_ptr<const standards::StandardRegistry> registry);
    ~ReportGenerationRunner();

    ReportGenerationRunner(const ReportGenerationRunner&) = delete;
    ReportGenerationRunner& operator=(const ReportGenerationRunner&) = delete;

    void start();
    void stop();

    /// 排入一个任务。同一个任务重复排入是安全的：状态机只让处理中的任务往前走。
    void enqueue(std::string job_id);

    /// 把还停在处理中状态的任务判失败。
    ///
    /// 进程重启后没人接着跑它们，留着会永远占住"同一用户同一年度只允许一个进行中
    /// 任务"的名额，用户再也点不了生成。
    int fail_interrupted_jobs() const;

    /// 到期清理，供定时器调用（设计 §17.1）。
    db::JobCleanupSummary cleanup() const;

    /// 成品的绝对路径。只有 ready 且文件确实还在时才返回。
    std::optional<std::filesystem::path> output_path(
        const ReportGenerationJob& job) const;

    /// 任务临时目录的根。
    const std::filesystem::path& job_root() const { return job_root_; }

private:
    /// 一次生成失败时抛这个，携带稳定错误码。
    struct JobFailure {
        std::string code;
        std::string message;
    };

    void worker();
    void run(const std::string& job_id);

    /// 调 Python 工具服务的一个接口；非 200 一律按失败抛出。
    Json::Value call_python(
        const std::string& path, const Json::Value& body, double timeout_seconds,
        const std::string& failure_code) const;

    drogon::orm::DbClientPtr client_;
    config::AppConfig config_;
    std::shared_ptr<const standards::StandardRegistry> registry_;
    std::filesystem::path job_root_;

    std::mutex mutex_;
    std::condition_variable pending_;
    std::deque<std::string> queue_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace bridge_report::report
