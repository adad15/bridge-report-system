#include "bridge_report/archive/BridgeMediaArchive.hpp"

#include <fstream>

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/archive/ImageContent.hpp"

namespace bridge_report::archive {

ArchivedBridgeMediaFile archive_bridge_media(
    std::string_view content,
    std::string_view original_file_name,
    const BridgeMediaArchiveContext& context) {
    if (context.max_bytes > 0 && content.size() > context.max_bytes) {
        throw BridgeMediaTooLargeError("bridge media exceeds the configured size limit");
    }

    // 只信内容。浏览器给的文件名是用户写的，而写进归档的扩展名会被后续所有环节当作事实。
    const auto extension = detect_image_extension(content);
    if (extension.empty()) throw BridgeMediaArchiveError("bridge media is not a supported image");

    const auto hash = sha256_hex(content);
    if (hash.empty()) throw BridgeMediaArchiveError("unable to hash bridge media");

    // 文件名带内容哈希：同一槽位换图时新旧文件不同名，删旧的不会碰到新的。
    const auto file_name = context.slot + "-" + hash.substr(0, 12) + extension;
    const auto relative = build_bridge_media_relative_path(
        context.bridge_system_number, context.slot, file_name);
    const auto absolute = resolve_path_under_root(context.archive_root, relative);

    std::error_code code;
    std::filesystem::create_directories(absolute.parent_path(), code);
    if (code) throw BridgeMediaArchiveError("unable to create the bridge media directory");

    std::ofstream output(absolute, std::ios::binary | std::ios::trunc);
    if (!output) throw BridgeMediaArchiveError("unable to open the bridge media file for writing");
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    if (!output) {
        std::filesystem::remove(absolute, code);
        throw BridgeMediaArchiveError("unable to write the bridge media file");
    }

    ArchivedBridgeMediaFile archived;
    archived.original_file_name = std::string(original_file_name);
    archived.storage_relative_path = relative.generic_string();
    archived.file_extension = extension;
    archived.file_size_bytes = content.size();
    archived.sha256 = hash;
    return archived;
}

void remove_archived_bridge_media(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& storage_relative_path,
    const std::string& sha256) noexcept {
    try {
        if (!is_safe_archive_relative_path(storage_relative_path)) return;
        const auto absolute = resolve_path_under_root(archive_root, storage_relative_path);
        if (!std::filesystem::is_regular_file(absolute)) return;

        // 内容对不上说明这不是我们以为的那个文件，留着比误删强。
        std::string content;
        {
            // 读完就关：Windows 上文件还开着句柄就删不掉，而下面的删除是静默失败的。
            std::ifstream input(absolute, std::ios::binary);
            if (!input) return;
            content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
        if (!sha256.empty() && sha256_hex(content) != sha256) return;

        std::error_code code;
        std::filesystem::remove(absolute, code);
    } catch (...) {
        // 清理失败不该影响业务结果：数据库那一侧已经改完了。
    }
}

}  // namespace bridge_report::archive
