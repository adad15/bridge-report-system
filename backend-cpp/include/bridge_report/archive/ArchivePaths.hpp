#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace bridge_report::archive {

std::string sanitize_path_part(std::string_view value);

std::filesystem::path build_import_input_relative_path(
    std::string_view bridge_number,
    std::string_view bridge_name,
    int inspection_year,
    std::string_view import_number,
    std::string_view import_name,
    std::string_view file_number,
    std::string_view original_file_name
);

std::filesystem::path build_import_photo_relative_path(
    std::string_view bridge_number,
    std::string_view bridge_name,
    int inspection_year,
    std::string_view import_number,
    std::string_view import_name,
    std::string_view file_number,
    std::string_view photo_file_name
);

bool is_safe_archive_relative_path(const std::filesystem::path& path);

}  // namespace bridge_report::archive
