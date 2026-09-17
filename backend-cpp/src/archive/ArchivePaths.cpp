#include "bridge_report/archive/ArchivePaths.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>

namespace bridge_report::archive {

namespace {

bool is_unsafe_path_char(char value) {
    switch (value) {
        case '<':
        case '>':
        case ':':
        case '"':
        case '/':
        case '\\':
        case '|':
        case '?':
        case '*':
            return true;
        default:
            return static_cast<unsigned char>(value) < 32;
    }
}

std::string numbered_name(std::string_view number, std::string_view name) {
    std::ostringstream output;
    output << sanitize_path_part(number) << "_" << sanitize_path_part(name);
    return output.str();
}

}  // 匿名命名空间

std::string sanitize_path_part(std::string_view value) {
    std::string result;
    result.reserve(value.size());

    for (const char character : value) {
        result.push_back(is_unsafe_path_char(character) ? '_' : character);
    }

    // Windows 文件名不能稳定地保留末尾空格或句点。
    while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
        result.pop_back();
    }

    if (result.empty()) {
        return "_";
    }

    return result;
}

std::filesystem::path build_bridge_media_relative_path(
    std::string_view bridge_number,
    std::string_view slot,
    std::string_view file_name
) {
    // 路径格式：bridges/<系统编号>/media/<slot>/<file>，全是 ASCII。
    return std::filesystem::path("bridges")
        / sanitize_path_part(bridge_number)
        / "media"
        / sanitize_path_part(slot)
        / sanitize_path_part(file_name);
}

std::filesystem::path build_import_input_relative_path(
    std::string_view bridge_number,
    std::string_view bridge_name,
    int inspection_year,
    std::string_view import_number,
    std::string_view import_name,
    std::string_view file_number,
    std::string_view original_file_name
) {
    // 路径格式：bridges/<bridge>/<year>/imports/<import>/input/<file>
    return std::filesystem::path("bridges")
        / numbered_name(bridge_number, bridge_name)
        / std::to_string(inspection_year)
        / "imports"
        / numbered_name(import_number, import_name)
        / "input"
        / numbered_name(file_number, original_file_name);
}

std::filesystem::path build_import_photo_relative_path(
    std::string_view bridge_number,
    std::string_view bridge_name,
    int inspection_year,
    std::string_view import_number,
    std::string_view import_name,
    std::string_view file_number,
    std::string_view photo_file_name
) {
    // 路径格式：bridges/<bridge>/<year>/imports/<import>/photos/<file>
    return std::filesystem::path("bridges")
        / numbered_name(bridge_number, bridge_name)
        / std::to_string(inspection_year)
        / "imports"
        / numbered_name(import_number, import_name)
        / "photos"
        / numbered_name(file_number, photo_file_name);
}

bool is_safe_archive_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path == "." || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }

    for (const auto& part : path) {
        if (part == "..") {
            return false;
        }
    }

    return true;
}

std::filesystem::path resolve_path_under_root(
    const std::filesystem::path& root,
    const std::filesystem::path& relative_path
) {
    if (!is_safe_archive_relative_path(relative_path)) {
        throw std::invalid_argument("archive path must be a safe relative path");
    }
    const auto normalized_root = std::filesystem::weakly_canonical(root);
    const auto resolved = std::filesystem::weakly_canonical(normalized_root / relative_path);
    auto root_it = normalized_root.begin();
    auto resolved_it = resolved.begin();
    for (; root_it != normalized_root.end(); ++root_it, ++resolved_it) {
        if (resolved_it == resolved.end() || *root_it != *resolved_it) {
            throw std::invalid_argument("archive path escapes configured root");
        }
    }
    return resolved;
}

}  // 命名空间 bridge_report::archive
