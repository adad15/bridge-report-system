#include "bridge_report/report/ReportGenerationRunner.hpp"

#include <exception>
#include <fstream>
#include <system_error>
#include <utility>

#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <trantor/utils/Logger.h>

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/db/ReportContextRepository.hpp"
#include "bridge_report/db/ReportPreflightRepository.hpp"
#include "bridge_report/db/ReportTemplateRepository.hpp"

namespace bridge_report::report {
namespace {

namespace fs = std::filesystem;

constexpr const char* kAssemblePath = "/reports/assemble";
constexpr const char* kFieldUpdatePath = "/reports/fields/update";
constexpr const char* kValidateOutputPath = "/reports/output/validate";

/// 装配和校验给得比较宽：一份 300 行病害表、400 张照片的报告实测装配 65 秒，
/// 但真实规模的上限还没摸到（§25.3 的规模验收还没跑）。
constexpr double kAssembleTimeoutSeconds = 1800.0;
constexpr double kValidateTimeoutSeconds = 300.0;
/// 刷域要等全局队列，队列本身上限 1800 秒，HTTP 这一层必须比它宽。
constexpr double kFieldUpdateSlackSeconds = 2400.0;

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

std::string detail_text(const Json::Value& body, const char* key) {
    const auto& detail = body["detail"];
    if (detail.isObject() && detail[key].isString()) return detail[key].asString();
    return {};
}

void remove_quietly(const fs::path& path) {
    std::error_code error;
    fs::remove_all(path, error);
}

}  // namespace

ReportGenerationRunner::ReportGenerationRunner(
    drogon::orm::DbClientPtr client,
    config::AppConfig config,
    std::shared_ptr<const standards::StandardRegistry> registry)
    : client_(std::move(client)),
      config_(std::move(config)),
      registry_(std::move(registry)) {
    std::error_code error;
    job_root_ = fs::weakly_canonical(config_.temporary_report_root, error);
    if (error) job_root_ = config_.temporary_report_root;
    fs::create_directories(job_root_, error);
}

ReportGenerationRunner::~ReportGenerationRunner() { stop(); }

void ReportGenerationRunner::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    worker_ = std::thread([this] { worker(); });
}

void ReportGenerationRunner::stop() {
    if (!running_.exchange(false)) return;
    pending_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void ReportGenerationRunner::enqueue(std::string job_id) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        queue_.push_back(std::move(job_id));
    }
    pending_.notify_one();
}

void ReportGenerationRunner::worker() {
    while (running_.load()) {
        std::string job_id;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            pending_.wait(lock, [this] { return !queue_.empty() || !running_.load(); });
            if (!running_.load()) return;
            job_id = std::move(queue_.front());
            queue_.pop_front();
        }
        try {
            run(job_id);
        } catch (const std::exception& error) {
            // 走到这里说明连"记一次失败"都出了问题（多半是数据库没了）。任务会停在
            // 处理中状态，下次启动的 fail_interrupted_jobs 会收拾它。
            LOG_ERROR << "报告生成任务 " << job_id << " 异常终止: " << error.what();
        }
    }
}

Json::Value ReportGenerationRunner::call_python(
    const std::string& path, const Json::Value& body, double timeout_seconds,
    const std::string& failure_code) const {
    auto client = drogon::HttpClient::newHttpClient(config_.python_tools_base_url);
    auto request = drogon::HttpRequest::newHttpJsonRequest(body);
    request->setMethod(drogon::Post);
    request->setPath(path);

    const auto [result, response] = client->sendRequest(request, timeout_seconds);
    if (result != drogon::ReqResult::Ok || !response) {
        throw JobFailure{kJobErrorToolsUnavailable,
                         "Python 工具服务没有响应，报告未生成。请确认它已启动。"};
    }
    const auto payload = response->getJsonObject();
    if (!payload) {
        throw JobFailure{kJobErrorToolsUnavailable,
                         "Python 工具服务返回了无法解析的内容，报告未生成。"};
    }
    if (response->statusCode() != drogon::k200OK) {
        const auto code = detail_text(*payload, "code");
        const auto message = detail_text(*payload, "message");
        throw JobFailure{code.empty() ? failure_code : code,
                         message.empty() ? "Python 工具服务返回了失败。" : message};
    }
    return *payload;
}

void ReportGenerationRunner::run(const std::string& job_id) {
    db::ReportGenerationJobRepository jobs(client_);
    const auto job = jobs.find(job_id);
    if (!job.has_value() || !job_is_running(job->status)) return;

    const auto directory = job_root_ / job_id;
    Json::Value progress(Json::objectValue);

    try {
        // ---- 1. 数据校验（设计 §16） --------------------------------------
        jobs.advance(job_id, JobStatus::ValidatingData, progress);

        db::ReportPreflightRepository preflight(client_);
        const auto checked = preflight.evaluate(job->inspection_year_id);
        if (!checked.has_value()) {
            throw JobFailure{kJobErrorContextUnavailable, "年度检查不存在。"};
        }
        if (!checked->can_generate()) {
            Json::Value blocking(Json::arrayValue);
            std::string summary;
            for (const auto& finding : checked->findings) {
                if (finding.severity != PreflightSeverity::Blocking) continue;
                blocking.append(finding.to_json());
                if (!summary.empty()) summary += "；";
                summary += finding.message;
            }
            progress["blocking_findings"] = blocking;
            jobs.advance(job_id, JobStatus::ValidatingData, progress);
            throw JobFailure{kJobErrorPreflightBlocked,
                             summary.empty() ? "生成前检查未通过。" : summary};
        }

        db::ReportTemplateRepository templates(client_);
        db::ReportContextRepository contexts(client_, registry_);
        const auto context = contexts.build(job->inspection_year_id);
        if (!context.has_value()) {
            throw JobFailure{kJobErrorContextUnavailable,
                             "组装报告上下文失败：年度检查不存在或尚未配置模板。"};
        }
        const auto relative = templates.find_file_relative_path(context->template_id);
        if (!relative.has_value()) {
            throw JobFailure{kJobErrorTemplateMissing, "所选模板已不存在。"};
        }
        fs::path template_path;
        try {
            template_path = archive::resolve_path_under_root(config_.archive_root, *relative);
        } catch (const std::exception&) {
            throw JobFailure{kJobErrorTemplateFileMissing, "模板文件路径不合法。"};
        }
        if (!fs::is_regular_file(template_path)) {
            throw JobFailure{kJobErrorTemplateFileMissing,
                             "模板文件在归档里找不到，请重新上传模板。"};
        }

        // ---- 2. 落盘上下文 ------------------------------------------------
        std::error_code error;
        fs::create_directories(directory, error);
        if (error) {
            throw JobFailure{kJobErrorUnexpected,
                             "创建任务临时目录失败：" + error.message()};
        }
        const auto context_path = directory / "context.json";
        {
            std::ofstream output(context_path, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw JobFailure{kJobErrorUnexpected, "写入上下文文件失败。"};
            }
            output << compact_json(context->to_json());
        }

        // ---- 3. 装配（设计 §18） ------------------------------------------
        const auto assembled_path = directory / "assembled.docx";
        jobs.advance(job_id, JobStatus::AssemblingDocx, progress);

        Json::Value assemble_body;
        assemble_body["template_path"] = template_path.generic_string();
        assemble_body["context_path"] = context_path.generic_string();
        assemble_body["archive_root"] =
            fs::weakly_canonical(config_.archive_root, error).generic_string();
        assemble_body["output_path"] = assembled_path.generic_string();
        const auto assembled = call_python(
            kAssemblePath, assemble_body, kAssembleTimeoutSeconds, kJobErrorAssembleFailed);

        progress["blocks_rendered"] = assembled["blocks_rendered"];
        progress["assemble_seconds"] = assembled["elapsed_seconds"];
        if (assembled["missing_placeholders"].isArray() &&
            !assembled["missing_placeholders"].empty()) {
            progress["missing_placeholders"] = assembled["missing_placeholders"];
        }

        // ---- 4. 刷域（设计 §19） ------------------------------------------
        const auto final_path = directory / "report.docx";
        jobs.advance(job_id, JobStatus::UpdatingFields, progress);

        Json::Value update_body;
        update_body["input_path"] = assembled_path.generic_string();
        update_body["output_path"] = final_path.generic_string();
        update_body["timeout_seconds"] = config_.report_field_update_timeout_seconds;
        const auto updated = call_python(
            kFieldUpdatePath, update_body,
            config_.report_field_update_timeout_seconds + kFieldUpdateSlackSeconds,
            kJobErrorFieldUpdateFailed);

        progress["updater"] = updated["updater"];
        progress["page_count"] = updated["page_count"];
        progress["field_update_seconds"] = updated["elapsed_seconds"];
        progress["field_update_queued_seconds"] = updated["queued_seconds"];

        // ---- 5. 最终校验（设计 §20） --------------------------------------
        jobs.advance(job_id, JobStatus::ValidatingDocx, progress);

        Json::Value validate_body;
        validate_body["docx_path"] = final_path.generic_string();
        validate_body["job_directory"] = directory.generic_string();
        validate_body["blocks_rendered"] = assembled["blocks_rendered"];
        const auto validated = call_python(
            kValidateOutputPath, validate_body, kValidateTimeoutSeconds,
            kJobErrorOutputInvalid);
        if (validated["status"].asString() != "valid") {
            std::string summary;
            for (const auto& issue : validated["issues"]) {
                if (!summary.empty()) summary += "；";
                summary += issue["message"].asString();
            }
            throw JobFailure{kJobErrorOutputInvalid,
                             summary.empty() ? "成品未通过最终校验。" : summary};
        }
        progress["section_count"] = validated["section_count"];
        progress["image_count"] = validated["image_count"];

        // 装配中间件已经没用了，成品留下就行。
        remove_quietly(assembled_path);

        // 体积放进进度里：一份大报告二十多兆，点下载之前得让人知道要下多大的东西。
        const auto size = fs::file_size(final_path, error);
        if (!error) progress["file_bytes"] = static_cast<Json::UInt64>(size);

        ReportFilenameParts parts;
        parts.report_number = context->report_no;
        parts.administrative_region = context->administrative_region;
        parts.route_code = context->route_code;
        parts.route_name = context->route_name;
        parts.bridge_name = context->bridge_name;
        parts.overall_grade = context->overall_grade;

        jobs.mark_ready(job_id, final_path, build_report_filename(parts),
                        config_.report_retention_hours, progress);
        LOG_INFO << "报告生成任务 " << job_id << " 完成：" << final_path.generic_string();
    } catch (const JobFailure& failure) {
        // 失败不留文件：半成品既不能下载，也不该在盘上放着（设计 §17.1）。
        remove_quietly(directory);
        jobs.mark_failed(job_id, failure.code, failure.message);
        LOG_WARN << "报告生成任务 " << job_id << " 失败 [" << failure.code << "]: "
                 << failure.message;
    } catch (const std::exception& error) {
        remove_quietly(directory);
        jobs.mark_failed(job_id, kJobErrorUnexpected, error.what());
        LOG_ERROR << "报告生成任务 " << job_id << " 未预期失败: " << error.what();
    }
}

int ReportGenerationRunner::fail_interrupted_jobs() const {
    const auto rows = client_->execSqlSync(
        "update report_generation_jobs set status = 'failed', error_code = $1, "
        " error_message = $2, finished_at = now(), temporary_file_path = null "
        "where status in ('queued','validating_data','assembling_docx',"
        " 'updating_fields','validating_docx') returning id::text as id",
        std::string(kJobErrorInterrupted),
        std::string("服务重启时这个任务还没跑完，已作废。请重新生成。"));
    for (const auto& row : rows) {
        remove_quietly(job_root_ / row["id"].as<std::string>());
    }
    return static_cast<int>(rows.size());
}

db::JobCleanupSummary ReportGenerationRunner::cleanup() const {
    db::ReportGenerationJobRepository jobs(client_);
    return jobs.cleanup(job_root_, config_.report_job_history_days);
}

std::optional<fs::path> ReportGenerationRunner::output_path(
    const ReportGenerationJob& job) const {
    if (!job.can_download()) return std::nullopt;
    std::error_code error;
    const auto path = fs::weakly_canonical(fs::path(*job.temporary_file_path), error);
    if (error) return std::nullopt;
    // 路径来自数据库，交给 HTTP 之前必须自证它在受控临时根目录内（设计 §22）。
    const auto relation = path.lexically_relative(job_root_);
    if (relation.empty() || *relation.begin() == "..") return std::nullopt;
    if (!fs::is_regular_file(path)) return std::nullopt;
    return path;
}

}  // namespace bridge_report::report
