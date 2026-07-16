#include "bridge_report/deletion/TemporaryWordCleanupCoordinator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "bridge_report/archive/TemporaryWordStorage.hpp"

namespace bridge_report::deletion {
namespace {

struct ClaimedSource {
    std::string id;
    std::string relative_path;
    std::string cleanup_reason;
    int attempt_count{0};
};

std::string truncate_error(const std::exception& error) {
    auto message = std::string(error.what());
    if (message.size() > 2000) message.resize(2000);
    return message;
}

}  // namespace

TemporaryWordCleanupCoordinator::TemporaryWordCleanupCoordinator(
    drogon::orm::DbClientPtr db_client,
    std::filesystem::path temporary_word_root,
    TemporaryWordCleanupPolicy policy
) : db_client_(std::move(db_client)),
    temporary_word_root_(std::filesystem::absolute(std::move(temporary_word_root))),
    policy_(policy) {}

TemporaryWordCleanupSummary TemporaryWordCleanupCoordinator::process_pending() {
    TemporaryWordCleanupSummary summary;
    const auto recovered = db_client_->execSqlSync(
        "with recovered as ("
        " update import_source_files set status='解析失败',parsing_started_at=null,"
        " expires_at=now()+make_interval(hours=>$2::int),last_error='后端中断了上一次解析，可在保留期内重试。',"
        " cleanup_reason=null,next_cleanup_at=null,updated_at=now() "
        " where status='解析中' and parsing_started_at < now()-make_interval(secs=>$1::int) "
        " returning import_record_id"
        ") update import_records ir set import_status='解析失败',"
        " error_message='后端中断了上一次解析，可在保留期内重试。',finished_at=now(),updated_at=now() "
        "from recovered r where ir.id=r.import_record_id returning ir.id",
        policy_.parsing_timeout_seconds, policy_.failed_retention_hours);
    summary.recovered_parses = static_cast<int>(recovered.size());

    const auto rows = db_client_->execSqlSync(
        "with candidates as ("
        " select id,status from import_source_files "
        " where (status='解析失败' and expires_at<=now()) or status='待清理' "
        " or (status='清理失败' and coalesce(next_cleanup_at,now())<=now()) "
        " order by coalesce(expires_at,next_cleanup_at,updated_at),id for update skip locked limit $1::int"
        "), claimed as ("
        " update import_source_files sf set status='清理中',"
        " cleanup_reason=case when c.status='解析失败' then '已过期' else coalesce(sf.cleanup_reason,'解析成功') end,"
        " updated_at=now() from candidates c where sf.id=c.id "
        " returning sf.id::text,sf.storage_relative_path,sf.cleanup_reason,sf.cleanup_attempt_count"
        ") select * from claimed",
        policy_.batch_size);

    std::vector<ClaimedSource> claimed;
    claimed.reserve(rows.size());
    for (const auto& row : rows) {
        claimed.push_back({
            row["id"].as<std::string>(),
            row["storage_relative_path"].as<std::string>(),
            row["cleanup_reason"].as<std::string>(),
            row["cleanup_attempt_count"].as<int>()
        });
    }
    summary.claimed = static_cast<int>(claimed.size());

    for (const auto& item : claimed) {
        try {
            archive::remove_temporary_word(temporary_word_root_, item.relative_path);
            const auto updated = db_client_->execSqlSync(
                "update import_source_files set status=case when cleanup_reason='已过期' then '已过期' else '已删除' end,"
                "deleted_at=now(),last_error=null,next_cleanup_at=null,updated_at=now() "
                "where id=$1::uuid and status='清理中'", item.id);
            if (updated.affectedRows() > 0) {
                ++summary.completed;
                db_client_->execSqlSync(
                    "delete from import_source_files where id=$1::uuid and import_record_id is null "
                    "and status in ('已删除','已过期')", item.id);
            }
        } catch (const std::exception& error) {
            std::int64_t delay = policy_.retry_base_seconds;
            for (int attempt = 0; attempt < item.attempt_count && delay < policy_.retry_max_seconds; ++attempt) {
                delay = (std::min<std::int64_t>)(delay * 2, policy_.retry_max_seconds);
            }
            const auto updated = db_client_->execSqlSync(
                "update import_source_files set status='清理失败',last_error=$2,"
                "cleanup_attempt_count=cleanup_attempt_count+1,"
                "next_cleanup_at=now()+make_interval(secs=>$3::int),updated_at=now() "
                "where id=$1::uuid and status='清理中'",
                item.id, truncate_error(error), static_cast<int>(delay));
            if (updated.affectedRows() > 0) ++summary.failed;
        }
    }

    // 上传文件先落盘、数据库事务后提交。若进程恰好在两者之间退出，会留下没有数据库行的
    // UUID 文件；超过解析超时时间后将其视为孤立文件。宽限期避免扫描碰到仍在提交的上传。
    try {
        std::set<std::string> retained_paths;
        const auto retained = db_client_->execSqlSync(
            "select storage_relative_path from import_source_files "
            "where status not in ('已删除','已过期')");
        for (const auto& row : retained) {
            retained_paths.insert(row["storage_relative_path"].as<std::string>());
        }
        std::error_code error;
        if (std::filesystem::is_directory(temporary_word_root_, error) && !error) {
            const auto cutoff = std::filesystem::file_time_type::clock::now()
                - std::chrono::seconds(policy_.parsing_timeout_seconds);
            for (const auto& entry : std::filesystem::directory_iterator(temporary_word_root_)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".docx") continue;
                const auto relative = entry.path().filename().generic_string();
                if (retained_paths.contains(relative) || entry.last_write_time() > cutoff) continue;
                archive::remove_temporary_word(temporary_word_root_, relative);
                ++summary.orphaned_removed;
            }
        }
    } catch (...) {
        // 单次孤立文件扫描失败不影响已完成的数据库驱动清理；下个周期继续尝试。
    }
    return summary;
}

}  // namespace bridge_report::deletion
