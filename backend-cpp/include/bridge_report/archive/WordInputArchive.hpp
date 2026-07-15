#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace bridge_report::archive {

enum class WordInputValidationError {
    None,
    InvalidFile,
    FileTooLarge,
};

struct WordInputMetadata {
    std::string original_file_name;
    std::string file_extension;
    std::size_t file_size_bytes{0};
    std::string sha256;
};

struct WordInputValidationResult {
    WordInputValidationError error{WordInputValidationError::None};
    WordInputMetadata metadata;

    bool ok() const noexcept { return error == WordInputValidationError::None; }
};

WordInputValidationResult validate_word_input(
    std::string_view submitted_file_name,
    std::string_view content,
    std::size_t max_bytes
);

/**
 * 将已校验内容先写入同目录临时文件，再原子改名到归档目标。
 * relative_path 必须能被 resolve_path_under_root 验证。
 */
std::filesystem::path archive_word_input(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& relative_path,
    std::string_view content,
    std::string_view expected_sha256
);

void remove_archived_word_input(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& relative_path
) noexcept;

}  // namespace bridge_report::archive
