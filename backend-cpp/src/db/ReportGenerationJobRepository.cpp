#include "bridge_report/db/ReportGenerationJobRepository.hpp"
#include "bridge_report/report/ReportJobCleanupCoordinator.hpp"

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
    "download_filename, template_snapshot_json::text as template_snapshot_json, "
    "(expires_at is not null and expires_at <= now()) as download_expired";

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
    if (job.status == report::JobStatus::Ready && row["download_expired"].as<bool>())
        job.status = report::JobStatus::Expired;
    if (!row["template_snapshot_json"].isNull())
        job.template_snapshot = parse_json_object(row["template_snapshot_json"].as<std::string>());
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
                "with created as (insert into report_generation_jobs "
                "(inspection_year_id, requested_by_user_id, status, template_id, "
                " template_checksum) "
                "values ($1::uuid, $2::uuid, 'queued', nullif($3,'')::uuid, nullif($4,'')) "
                "returning *), tracked as (insert into report_job_artifacts(job_id) select id from created returning job_id) select ") + kJobColumns + " from created",
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
            "with ready as (update report_generation_jobs set status = 'ready', "
            " temporary_file_path = $2, download_filename = $3, progress_json = $4::jsonb, "
            " finished_at = now(), expires_at = now() + make_interval(hours => $5), "
            " error_code = null, error_message = null "
            "where id = $1::uuid and status in (") + kRunningStatuses + ") returning id), tracked as ("
        "update report_job_artifacts a set expected_output_path=$2 from ready r where a.job_id=r.id returning a.job_id) "
        "select id from ready",
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

int ReportGenerationJobRepository::schedule_cleanup() const {
    const auto expired = client_->execSqlSync(
        "update report_generation_jobs set status='expired', temporary_file_path=null, download_filename=null, "
        "progress_json='{}'::jsonb, error_message=null "
        "where status='ready' and expires_at<=now() returning id");
    client_->execSqlSync(
        "update report_job_artifacts a set cleanup_state='pending', next_attempt_at=now() "
        "where cleanup_state='active' and ("
        "exists(select 1 from report_generation_jobs j where j.id=a.job_id and j.status in ('expired','failed')) "
        "or not exists(select 1 from report_generation_jobs j where j.id=a.job_id))");
    return static_cast<int>(expired.size());
}

std::vector<JobCleanupClaim> ReportGenerationJobRepository::claim_cleanup(int limit) const {
    const auto rows = client_->execSqlSync(
        "with candidates as (select job_id from report_job_artifacts "
        "where (cleanup_state='pending' and next_attempt_at<=now()) or "
        "(cleanup_state='cleaning' and claimed_at<now()-interval '15 minutes') "
        "order by next_attempt_at,job_id limit $1::integer for update skip locked) "
        "update report_job_artifacts a set cleanup_state='cleaning', claimed_at=now(), "
        "claim_token=gen_random_uuid(), attempts=attempts+1 from candidates c where a.job_id=c.job_id "
        "returning a.job_id::text as id,a.claim_token::text as token,a.expected_output_path", limit);
    std::vector<JobCleanupClaim> claims;
    for (const auto& row : rows)
        claims.push_back({row["id"].as<std::string>(), row["token"].as<std::string>(),
                          optional_text(row,"expected_output_path")});
    return claims;
}

bool ReportGenerationJobRepository::finish_cleanup(
    const JobCleanupClaim& claim, bool success, const std::string& error) const {
    const auto rows = client_->execSqlSync(
        "with completed as (update report_job_artifacts set "
        "cleanup_state=case when $3 then 'cleaned' else 'pending' end, "
        "cleaned_at=case when $3 then now() else null end, last_error=nullif($4,''), "
        "next_attempt_at=now()+make_interval(secs=>least(3600,30*greatest(1,least(attempts,120)))), "
        "claim_token=null,claimed_at=null "
        "where job_id=$1::uuid and claim_token=$2::uuid and cleanup_state='cleaning' returning job_id), "
        "cleared as (update report_generation_jobs j set temporary_file_path=null,template_snapshot_json=null "
        "from completed c where j.id=c.job_id and $3 returning j.id) select job_id from completed",
        claim.job_id,claim.token,success,error.substr(0,1000));
    return !rows.empty();
}

void ReportGenerationJobRepository::block_cleanup(
    const std::string& job_id,const std::string& reason) const {
    client_->execSqlSync(
        "update report_job_artifacts set cleanup_state='blocked',last_error=$2 "
        "where job_id=$1::uuid and cleanup_state='active'",job_id,reason.substr(0,1000));
}

int ReportGenerationJobRepository::purge_cleaned(int history_days) const {
    const int days=history_days>0?history_days:7;
    const auto rows=client_->execSqlSync(
        "delete from report_generation_jobs j where status in ('expired','failed') "
        "and coalesce(finished_at,created_at)<now()-make_interval(days=>$1) "
        "and exists(select 1 from report_job_artifacts a where a.job_id=j.id and a.cleanup_state='cleaned') returning id",days);
    client_->execSqlSync(
        "delete from report_job_artifacts a where cleanup_state='cleaned' "
        "and cleaned_at<now()-make_interval(days=>$1) "
        "and not exists(select 1 from report_generation_jobs j where j.id=a.job_id)",days);
    return static_cast<int>(rows.size());
}

JobCleanupSummary ReportGenerationJobRepository::cleanup(const fs::path& job_root,int history_days) const {
    return report::ReportJobCleanupCoordinator(client_,job_root).run(history_days);
}

}  // namespace bridge_report::db
