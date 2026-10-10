#include "bridge_report/report/ReportJobCleanupCoordinator.hpp"
#include <stdexcept>
#include <regex>
#include <trantor/utils/Logger.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace bridge_report::report {
namespace {
namespace fs = std::filesystem;
void reject_link(const fs::path& path) {
    if (fs::is_symlink(fs::symlink_status(path))) {
        throw std::runtime_error("report_cleanup_link_rejected");
    }
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        throw std::runtime_error("report_cleanup_reparse_point_rejected");
    }
#endif
}
bool remove_directory(const fs::path& configured_root, const db::JobCleanupClaim& claim) {
    static const std::regex uuid("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
    if (!std::regex_match(claim.job_id, uuid)) throw std::runtime_error("report_cleanup_invalid_job_id");
    const auto root = fs::weakly_canonical(configured_root);
    const auto directory = root / claim.job_id;
    reject_link(directory);
    if (fs::weakly_canonical(directory).parent_path() != root)
        throw std::runtime_error("report_cleanup_outside_root");
    if (claim.expected_output_path &&
        fs::weakly_canonical(fs::path(*claim.expected_output_path)).parent_path() != directory)
        throw std::runtime_error("report_cleanup_output_path_mismatch");
    if (!fs::exists(directory)) return false; // Already removed: idempotent success.
    for (const auto& entry : fs::recursive_directory_iterator(directory)) reject_link(entry.path());
    // Validate again immediately before removal; no caller-supplied directory is used.
    reject_link(directory);
    if (fs::weakly_canonical(directory) != directory)
        throw std::runtime_error("report_cleanup_directory_changed");
    fs::remove_all(directory);
    return true;
}
}
ReportJobCleanupCoordinator::ReportJobCleanupCoordinator(
    drogon::orm::DbClientPtr client, fs::path root)
    : client_(std::move(client)), root_(std::move(root)) {}
db::JobCleanupSummary ReportJobCleanupCoordinator::run(int history_days) const {
    db::ReportGenerationJobRepository jobs(client_);
    db::JobCleanupSummary summary;
    summary.expired = jobs.schedule_cleanup();
    for (const auto& claim : jobs.claim_cleanup(25)) {
        bool removed = false;
        std::string failure;
        try { removed = remove_directory(root_, claim); }
        catch (const std::exception& error) {
            failure = error.what();
            LOG_WARN << "report cleanup deferred job=" << claim.job_id << " error=" << failure;
        }
        // A failed DB acknowledgement leaves the claim reclaimable; deletion is idempotent.
        if (jobs.finish_cleanup(claim, failure.empty(), failure) && removed)
            ++summary.files_removed;
    }
    summary.purged = jobs.purge_cleaned(history_days);
    return summary;
}
}
