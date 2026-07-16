#include "bridge_report/deletion/ArchiveFileDeletionCore.hpp"

#include <system_error>
#include <utility>

#include "bridge_report/archive/ArchivePaths.hpp"

namespace bridge_report::deletion {

ArchiveFileDeletionCore::ArchiveFileDeletionCore(std::filesystem::path archive_root)
    : archive_root_(std::move(archive_root)) {}

bool ArchiveFileDeletionCore::remove(const std::filesystem::path& storage_relative_path) const {
    const auto resolved = archive::resolve_path_under_root(archive_root_, storage_relative_path);
    std::error_code error;
    const bool removed = std::filesystem::remove(resolved, error);
    if (error) throw std::system_error(error);
    return removed;
}

}  // namespace bridge_report::deletion
