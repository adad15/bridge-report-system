#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bridge_report::archive {

class BridgeMediaArchiveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// 单独一类：调用方要把「太大」和「不是图片」映射到不同的 HTTP 状态。
class BridgeMediaTooLargeError : public BridgeMediaArchiveError {
public:
    using BridgeMediaArchiveError::BridgeMediaArchiveError;
};

struct BridgeMediaArchiveContext {
    std::filesystem::path archive_root;
    /// 路径里只用系统编号，不拼桥名，理由见 build_bridge_media_relative_path。
    std::string bridge_system_number;
    std::string slot;
    /// 0 表示不限；超限抛 BridgeMediaTooLargeError。
    std::uintmax_t max_bytes{0};
};

struct ArchivedBridgeMediaFile {
    std::string original_file_name;
    std::string storage_relative_path;
    std::string file_extension;
    std::uintmax_t file_size_bytes{0};
    std::string sha256;
};

/**
 * @brief 把一张桥梁图件写进归档。
 *
 * 校验全部在写盘之前完成，任何失败路径都不留下半个文件。落点按桥和槽位分目录，
 * 文件名带内容哈希，换图不会覆盖掉还被别处引用的旧文件。
 *
 * @throws BridgeMediaTooLargeError 内容超过 max_bytes。
 * @throws BridgeMediaArchiveError 内容不是支持的图片，或写盘失败。
 */
ArchivedBridgeMediaFile archive_bridge_media(
    std::string_view content,
    std::string_view original_file_name,
    const BridgeMediaArchiveContext& context);

/// 删除一张已归档图件；SHA-256 对不上就原样留着，宁可留垃圾也不误删。
void remove_archived_bridge_media(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& storage_relative_path,
    const std::string& sha256) noexcept;

}  // namespace bridge_report::archive
