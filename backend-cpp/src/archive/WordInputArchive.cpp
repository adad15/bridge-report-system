#include "bridge_report/archive/WordInputArchive.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <openssl/evp.h>

#include "bridge_report/archive/ArchivePaths.hpp"

namespace bridge_report::archive {
namespace {

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string file_extension(std::string_view file_name) {
    const auto separator = file_name.find_last_of("/\\");
    const auto dot = file_name.find_last_of('.');
    if (dot == std::string_view::npos || (separator != std::string_view::npos && dot < separator)) {
        return {};
    }
    return lower_ascii(std::string(file_name.substr(dot)));
}

std::string display_file_name(std::string_view submitted) {
    std::string value(submitted);
    std::replace(value.begin(), value.end(), '\\', '/');
    const auto separator = value.find_last_of('/');
    if (separator != std::string::npos) value.erase(0, separator + 1);
    return value;
}

std::string sha256_hex(std::string_view content) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_length = 0;
    if (EVP_Digest(content.data(), content.size(), digest.data(), &digest_length, EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("cannot calculate Word SHA-256");
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int index = 0; index < digest_length; ++index) {
        output << std::setw(2) << static_cast<int>(digest[index]);
    }
    return output.str();
}

}  // namespace

WordInputValidationResult validate_word_input(
    const std::string_view submitted_file_name,
    const std::string_view content,
    const std::size_t max_bytes
) {
    WordInputValidationResult result;
    result.metadata.original_file_name = display_file_name(submitted_file_name);
    result.metadata.file_size_bytes = content.size();
    if (result.metadata.original_file_name.empty() || content.empty()) {
        result.error = WordInputValidationError::InvalidFile;
        return result;
    }
    // HTTP multipart 文件名是 UTF-8 文本。这里只需检查 ASCII 扩展名，不应先转换为
    // Windows filesystem::path；文件名含全角符号或当前代码页无法表示的字符时，
    // 路径转换可能抛异常并把合法 .docx 误报成服务故障。
    result.metadata.file_extension = file_extension(result.metadata.original_file_name);
    if (result.metadata.file_extension != ".docx") {
        result.error = WordInputValidationError::InvalidFile;
        return result;
    }
    if (content.size() > max_bytes) {
        result.error = WordInputValidationError::FileTooLarge;
        return result;
    }
    result.metadata.sha256 = sha256_hex(content);
    return result;
}

std::filesystem::path archive_word_input(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& relative_path,
    const std::string_view content,
    const std::string_view expected_sha256
) {
    const auto destination = resolve_path_under_root(archive_root, relative_path);
    std::filesystem::create_directories(destination.parent_path());
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto temporary = destination;
    temporary += ".upload-" + suffix + ".tmp";
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.close();
        if (!output || std::filesystem::file_size(temporary) != content.size()
            || sha256_hex(content) != expected_sha256) {
            throw std::runtime_error("Word archive verification failed");
        }
        std::filesystem::rename(temporary, destination);
        return destination;
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

void remove_archived_word_input(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& relative_path
) noexcept {
    try {
        std::error_code ignored;
        std::filesystem::remove(resolve_path_under_root(archive_root, relative_path), ignored);
    } catch (...) {
    }
}

}  // namespace bridge_report::archive
