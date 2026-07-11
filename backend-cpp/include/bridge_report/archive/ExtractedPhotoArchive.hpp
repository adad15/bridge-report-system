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

ArchivedPhotoBatch archive_extracted_photos(const Json::Value& data, const PhotoArchiveContext& context);
void cleanup_archived_photo_batch(const std::filesystem::path& archive_root, const ArchivedPhotoBatch& batch) noexcept;

}  // namespace bridge_report::archive
