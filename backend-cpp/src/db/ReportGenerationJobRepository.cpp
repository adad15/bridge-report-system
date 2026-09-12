#include "bridge_report/db/ReportGenerationJobRepository.hpp"

#include <sstream>
#include <system_error>
#include <utility>

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>

namespace bridge_report::db {
namespace {

namespace fs = std::filesystem;

/// 每次读任务行都取同一组列，省得三处 SQL 各写一份而慢慢漂移。
constexpr const char* kJobColumns =
    "id::text as id, inspection_year_id::text as inspection_year_id, "
    "requested_by_user_id::text as requested_by_user_id, status, "
    "progress_json::text as progress_json, template_id::text as template_id, "
    "template_checksum, temporary_file_path, error_code, error_message, "
    "created_at::text as created_at, finished_at::text as finished_at, "
    "expires_at::text as expires_at, "
    "download_filename";

/// 进行中的状态，写在 SQL 里的那一份。与 job_is_running 必须一致。
constexpr const char* kRunningStatuses =
    "'queued','validating_data','assembling_docx','updating_fields','validating_docx'";

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

Json::Value parse_json_object(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value value;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &value, &errors) || !value.isObject()) {
        return Json::Value(Json::objectValue);
    }
    return value;
}

std::optional<std::string> optional_text(const drogon::orm::Row& row, const char* column) {
    const auto field = row[column];
    if (field.isNull()) return std::nullopt;
    return field.as<std::string>();
}

report::ReportGenerationJob read_job(const drogon::orm::Row& row) {
    report::ReportGenerationJob job;
    job.id = row["id"].as<std::string>();
    job.inspection_year_id = row["inspection_year_id"].as<std::string>();
    job.requested_by_user_id = row["requested_by_user_id"].as<std::string>();
    job.status = report::job_status_from_text(row["status"].as<std::string>())
                     .value_or(report::JobStatus::Queued);
    job.progress = parse_json_object(row["progress_json"].as<std::string>());
    job.template_id = optional_text(row, "template_id");
    job.template_checksum = optional_text(row, "template_checksum");
    job.temporary_file_path = optional_text(row, "temporary_file_path");
    job.error_code = optional_text(row, "error_code");
    job.error_message = optional_text(row, "error_message");
    job.created_at = row["created_at"].as<std::string>();
    job.finished_at = optional_text(row, "finished_at");
    job.expires_at = optional_text(row, "expires_at");
    job.download_filename = optional_text(row, "download_filename");
    return job;
}

/// 删掉一次任务留下的整个临时目录。
///
/// 只删受控根目录下的东西：路径来自数据库，删之前必须自证它在 job_root 里面。
/// 拿到什么删什么，一条被改过的路径就能删掉任意目录。
bool remove_job_files(const fs::path& job_root, const std::string& file_path) {
    std::error_code error;
    const auto root = fs::weakly_canonical(job_root, error);
    if (error) return false;
    const auto target = fs::weakly_canonical(fs::path(file_path), error);
    if (error) return false;

    auto directory = target.parent_path();
    // 成品就在任务目录里，任务目录就在根目录下。别的形状一律不动。
    if (directory.empty() || directory.parent_path() != root) return false;

    const auto removed = fs::remove_all(directory, error);
    return !error && removed > 0;
}

}  // namespace

ReportGenerationJobRepository::ReportGenerationJobRepository(drogon::orm::DbClientPtr client)
    : client_(std::move(client)) {}

JobCreation ReportGenerationJobRepository::create(
    const std::string& inspection_year_id,
    const std::string& requested_by_user_id,
    const std::optional<std::string>& template_id,
    const std::optional<std::string>& template_checksum) const {
    JobCreation creation;

    const auto years = client_->execSqlSync(
        "select 1 from inspection_years where id = $1::uuid", inspection_year_id);
    if (years.empty()) return creation;

    // 先查后插会有竞态，所以真正的防线是数据库的部分唯一索引；这里的查询只是为了
    // 常见情况下少抛一次异常，并把已有任务原样返回。
    const auto running = client_->execSqlSync(
        std::string("select ") + kJobColumns +
            " from report_generation_jobs where inspection_year_id = $1::uuid"
            " and requested_by_user_id = $2::uuid and status in (" + kRunningStatuses + ")"
            " order by created_at desc limit 1",
        inspection_year_id, requested_by_user_id);
    if (!running.empty()) {
        creation.outcome = JobCreation::Outcome::AlreadyRunning;
        creation.job = read_job(running[0]);
        return creation;
    }

    try {
        const auto inserted = client_->execSqlSync(
            std::string(
                "insert into report_generation_jobs "
                "(inspection_year_id, requested_by_user_id, status, template_id, "
                " template_checksum) "
                "values ($1::uuid, $2::uuid, 'queued', nullif($3,'')::uuid, nullif($4,'')) "
                "returning ") + kJobColumns,
            inspection_year_id, requested_by_user_id,
            template_id.value_or(std::string()),
            template_checksum.value_or(std::string()));
        creation.outcome = JobCreation::Outcome::Created;
        creation.job = read_job(inserted[0]);
        return creation;
    } catch (const drogon::orm::DrogonDbException&) {
        // 唯一索引挡住了并发的第二次提交：把已有的那个返回回去（设计 §17.3）。
        const auto existing = client_->execSqlSync(
            std::string("select ") + kJobColumns +
                " from report_generation_jobs where inspection_year_id = $1::uuid"
                " and requested_by_user_id = $2::uuid and status in ("
                + kRunningStatuses + ") order by created_at desc limit 1",
            inspection_year_id, requested_by_user_id);
        if (existing.empty()) throw;
        creation.outcome = JobCreation::Outcome::AlreadyRunning;
        creation.job = read_job(existing[0]);
        return creation;
    }
}

std::optional<report::ReportGenerationJob> ReportGenerationJobRepository::find(
    const std::string& job_id) const {
    const auto rows = client_->execSqlSync(
        std::string("select ") + kJobColumns +
            " from report_generation_jobs where id = $1::uuid",
        job_id);
    if (rows.empty()) return std::nullopt;
    return read_job(rows[0]);
}

std::optional<report::ReportGenerationJob> ReportGenerationJobRepository::find_current(
    const std::string& inspection_year_id) const {
    const auto rows = client_->execSqlSync(
        std::string("select ") + kJobColumns +
            " from report_generation_jobs where inspection_year_id = $1::uuid"
            " order by created_at desc limit 1",
        inspection_year_id);
    if (rows.empty()) return std::nullopt;
    return read_job(rows[0]);
}

bool ReportGenerationJobRepository::advance(
    const std::string& job_id,
    report::JobStatus status,
    const Json::Value& progress) const {
    // 只推进还在跑的任务：一个迟到的阶段回调不能把已经失败的任务改回处理中。
    const auto rows = client_->execSqlSync(
        std::string(
            "update report_generation_jobs set status = $2, progress_json = $3::jsonb "
            "where id = $1::uuid and status in (") + kRunningStatuses + ") returning id",
        job_id, report::job_status_text(status), compact_json(progress));
    return !rows.empty();
}

bool ReportGenerationJobRepository::mark_ready(
    const std::string& job_id,
    const fs::path& output_path,
    const std::string& download_filename,
    int retention_hours,
    const Json::Value& progress) const {
    const auto rows = client_->execSqlSync(
        std::string(
            "update report_generation_jobs set status = 'ready', "
            " temporary_file_path = $2, download_filename = $3, progress_json = $4::jsonb, "
            " finished_at = now(), expires_at = now() + make_interval(hours => $5), "
            " error_code = null, error_message = null "
            "where id = $1::uuid and status in (") + kRunningStatuses + ") returning id",
        job_id, output_path.generic_string(), download_filename, compact_json(progress),
        retention_hours > 0 ? retention_hours : 24);
    return !rows.empty();
}

bool ReportGenerationJobRepository::mark_failed(
    const std::string& job_id,
    const std::string& error_code,
    const std::string& error_message) const {
    const auto rows = client_->execSqlSync(
        std::string(
            "update report_generation_jobs set status = 'failed', error_code = $2, "
            " error_message = nullif($3,''), finished_at = now(), "
            " temporary_file_path = null "
            "where id = $1::uuid and status in (") + kRunningStatuses + ") returning id",
        job_id, error_code, error_message);
    return !rows.empty();
}

JobCleanupSummary ReportGenerationJobRepository::cleanup(
    const fs::path& job_root, int history_days) const {
    JobCleanupSummary summary;

    // 到期的成品：先删文件，再改行。反过来做，一旦中途崩掉，数据库说没文件而文件
    // 还在盘上，就再也没人去删它了。
    const auto due = client_->execSqlSync(
        "select id::text as id, temporary_file_path from report_generation_jobs "
        "where expires_at is not null and expires_at <= now() "
        " and status <> 'expired' and temporary_file_path is not null");
    for (const auto& row : due) {
        const auto path = optional_text(row, "temporary_file_path");
        if (path.has_value() && remove_job_files(job_root, *path)) {
            summary.files_removed += 1;
        }
        // 诊断正文可能含业务内容，到期一并清空（设计 §17.1）。
        const auto updated = client_->execSqlSync(
            "update report_generation_jobs set status = 'expired', "
            " temporary_file_path = null, download_filename = null, "
            " progress_json = '{}'::jsonb, error_message = null, "
            " finished_at = coalesce(finished_at, now()) "
            "where id = $1::uuid returning id",
            row["id"].as<std::string>());
        if (!updated.empty()) summary.expired += 1;
    }

    // 过了保留期的终态行物理删除。运行中的任务不在此列，等它自己走完。
    const auto purged = client_->execSqlSync(
        "delete from report_generation_jobs "
        "where status in ('expired','failed') "
        " and coalesce(finished_at, created_at) < now() - make_interval(days => $1) "
        "returning id",
        history_days > 0 ? history_days : 7);
    summary.purged = static_cast<int>(purged.size());
    return summary;
}

}  // namespace bridge_report::db
