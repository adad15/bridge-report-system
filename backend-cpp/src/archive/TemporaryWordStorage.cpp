#include "bridge_report/archive/TemporaryWordStorage.hpp"

#include <cctype>
#include <stdexcept>
#include <system_error>

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/archive/WordInputArchive.hpp"

namespace bridge_report::archive {
namespace {

bool is_uuid_text(const std::string_view value) {
    if (value.size() != 36) return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(value[index]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::filesystem::path temporary_word_relative_path(const std::string_view source_file_id) {
    if (!is_uuid_text(source_file_id)) {
        throw std::invalid_argument("temporary Word source id must be a UUID");
    }
    return std::filesystem::path(std::string(source_file_id) + ".docx");
}

std::filesystem::path store_temporary_word(
    const std::filesystem::path& temporary_root,
    const std::filesystem::path& relative_path,
    const std::string_view content,
    const std::string_view expected_sha256
) {
    return archive_word_input(temporary_root, relative_path, content, expected_sha256);
}

void remove_temporary_word(
    const std::filesystem::path& temporary_root,
    const std::filesystem::path& relative_path
) {
    const auto resolved = resolve_path_under_root(temporary_root, relative_path);
    std::error_code error;
    const bool removed = std::filesystem::remove(resolved, error);
    if (error) throw std::system_error(error, "cannot remove temporary Word input");
    if (!removed && std::filesystem::exists(resolved, error)) {
        if (error) throw std::system_error(error, "cannot inspect temporary Word input");
        throw std::runtime_error("temporary Word input was not removed");
    }
}

}  // namespace bridge_report::archive
