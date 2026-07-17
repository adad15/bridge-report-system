#include "bridge_report/deletion/ArchiveFileDeletionCore.hpp"

#include <system_error>
#include <stdexcept>
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

bool ArchiveFileDeletionCore::remove_tree(
    const std::filesystem::path& storage_relative_path,
    const std::filesystem::path& required_relative_root
) const {
    const auto normalized = storage_relative_path.lexically_normal();
    const auto required = required_relative_root.lexically_normal();
    auto candidate = normalized.begin();
    for (auto allowed = required.begin(); allowed != required.end(); ++allowed, ++candidate) {
        if (candidate == normalized.end() || *candidate != *allowed) {
            throw std::invalid_argument("recursive deletion path is outside the allowed subtree");
        }
    }
    if (candidate == normalized.end()) {
        throw std::invalid_argument("recursive deletion cannot remove the allowed subtree root");
    }
    const auto resolved = archive::resolve_path_under_root(archive_root_, normalized);
    std::error_code error;
    if (!std::filesystem::exists(resolved, error)) {
        if (error) throw std::system_error(error);
        return false;
    }
    const auto removed = std::filesystem::remove_all(resolved, error);
    if (error) throw std::system_error(error);
    return removed > 0;
}

}  // namespace bridge_report::deletion
