#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <json/value.h>

namespace bridge_report::archive {

class PhotoArchiveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// 单独一类：调用方要把「太大」和「不是图片」映射到不同的 HTTP 状态。
class PhotoTooLargeError : public PhotoArchiveError {
public:
    using PhotoArchiveError::PhotoArchiveError;
};

struct PhotoArchiveContext {
    std::filesystem::path staging_root;
    std::filesystem::path archive_root;
    std::string bridge_system_number;
    std::string bridge_name;
    int inspection_year{0};
    std::string import_record_system_number;
    std::string import_name;
};

struct ArchivedPhotoFile {
    std::string candidate_id;
    std::string original_file_name;
    std::string current_file_name;
    std::filesystem::path storage_relative_path;
    std::string file_extension;
    std::uintmax_t file_size_bytes{0};
    std::string sha256;
    bool created_by_batch{false};
};

struct ArchivedPhotoBatch {
    Json::Value data;
    std::vector<ArchivedPhotoFile> files;
};

/// 一张人工上传的照片：内容在内存里，没有暂存文件可依赖。
struct UploadedPhotoInput {
    std::string content;
    std::string original_file_name;
    std::string candidate_id;
    /// 0 表示不限；超限抛 PhotoTooLargeError。
    std::uintmax_t max_bytes{0};
};

ArchivedPhotoBatch archive_extracted_photos(const Json::Value& data, const PhotoArchiveContext& context);
void cleanup_archived_photo_batch(const std::filesystem::path& archive_root, const ArchivedPhotoBatch& batch) noexcept;

/**
 * @brief 归档一张人工上传的照片，落点与 Word 抽出的照片同一目录。
 * @throws PhotoTooLargeError 内容超过 max_bytes。
 * @throws PhotoArchiveError 内容不是支持的图片，或扩展名与内容不符。
 *
 * 校验全部在写盘之前完成，任何失败路径都不留下半个文件。
 */
ArchivedPhotoFile archive_uploaded_photo(const UploadedPhotoInput& input, const PhotoArchiveContext& context);

/// 删除一张已归档照片；SHA-256 对不上就原样留着，宁可留垃圾也不误删。
void remove_archived_photo(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& storage_relative_path,
    const std::string& sha256
) noexcept;

}  // namespace bridge_report::archive
