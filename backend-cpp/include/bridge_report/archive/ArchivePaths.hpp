#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace bridge_report::archive {

/**
 * @brief 清理单个归档路径片段中的危险字符。
 * @return 可作为文件名或目录名片段使用的字符串；空结果返回 "_"。
 */
std::string sanitize_path_part(std::string_view value);

/**
 * @brief 构建导入原始文件在 archive 根目录下的相对路径。
 */
std::filesystem::path build_import_input_relative_path(
    std::string_view bridge_number,
    std::string_view bridge_name,
    int inspection_year,
    std::string_view import_number,
    std::string_view import_name,
    std::string_view file_number,
    std::string_view original_file_name
);

/**
 * @brief 构建导入照片在 archive 根目录下的相对路径。
 */
std::filesystem::path build_import_photo_relative_path(
    std::string_view bridge_number,
    std::string_view bridge_name,
    int inspection_year,
    std::string_view import_number,
    std::string_view import_name,
    std::string_view file_number,
    std::string_view photo_file_name
);

/**
 * @brief 拒绝空路径、根路径、绝对路径，以及包含 ".." 的归档路径。
 */
bool is_safe_archive_relative_path(const std::filesystem::path& path);

}  // 命名空间 bridge_report::archive
