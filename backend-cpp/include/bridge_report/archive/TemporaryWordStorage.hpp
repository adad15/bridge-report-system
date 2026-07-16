#pragma once

#include <filesystem>
#include <string_view>

namespace bridge_report::archive {

std::filesystem::path temporary_word_relative_path(std::string_view source_file_id);

std::filesystem::path store_temporary_word(
    const std::filesystem::path& temporary_root,
    const std::filesystem::path& relative_path,
    std::string_view content,
    std::string_view expected_sha256
);

void remove_temporary_word(
    const std::filesystem::path& temporary_root,
    const std::filesystem::path& relative_path
);

}  // namespace bridge_report::archive
