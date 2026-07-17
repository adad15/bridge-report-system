#pragma once

#include <filesystem>

namespace bridge_report::deletion {

class ArchiveFileDeletionCore {
public:
    explicit ArchiveFileDeletionCore(std::filesystem::path archive_root);

    // 返回 true 表示本次删除了文件，false 表示文件原本已不存在。
    bool remove(const std::filesystem::path& storage_relative_path) const;
    // 只允许递归删除 required_relative_root 之下的子目录，不允许删除该根本身。
    bool remove_tree(
        const std::filesystem::path& storage_relative_path,
        const std::filesystem::path& required_relative_root
    ) const;

private:
    std::filesystem::path archive_root_;
};

}  // namespace bridge_report::deletion
