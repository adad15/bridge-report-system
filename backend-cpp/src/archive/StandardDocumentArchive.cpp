#include "bridge_report/archive/StandardDocumentArchive.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#include <drogon/utils/Utilities.h>

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/archive/ImageContent.hpp"

namespace bridge_report::archive {

bool looks_like_pdf(std::string_view content, std::string_view original_file_name) {
    if (original_file_name.size() < 4) return false;
    std::string extension(original_file_name.substr(original_file_name.size() - 4));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".pdf" && content.substr(0, 5) == "%PDF-";
}

std::filesystem::path build_standard_document_relative_path(
    std::string_view document_id, std::string_view file_name) {
    return std::filesystem::path("standards") / sanitize_path_part(document_id) / sanitize_path_part(file_name);
}

ArchivedStandardPdf archive_standard_pdf(
    std::string_view content,
    std::string_view original_file_name,
    const std::filesystem::path& archive_root,
    std::string_view document_id) {
    if (content.empty()) throw StandardDocumentEmptyError("standard document upload is empty");
    // 只信内容和扩展名。浏览器报的 MIME 是客户端说了算的。
    if (!looks_like_pdf(content, original_file_name)) {
        throw StandardDocumentNotPdfError("standard document upload is not a PDF");
    }
    const auto hash = sha256_hex(content);
    if (hash.empty()) throw StandardDocumentArchiveError("unable to hash the standard document");

    const auto relative = build_standard_document_relative_path(document_id, drogon::utils::getUuid() + ".pdf");
    const auto absolute = resolve_path_under_root(archive_root, relative);

    std::error_code code;
    std::filesystem::create_directories(absolute.parent_path(), code);
    if (code) throw StandardDocumentArchiveError("unable to create the standard document directory");

    auto staging = absolute;
    staging += ".uploading";
    {
        std::ofstream output(staging, std::ios::binary | std::ios::trunc);
        if (!output) throw StandardDocumentArchiveError("unable to open the standard document for writing");
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.close();
        if (!output) {
            std::filesystem::remove(staging, code);
            throw StandardDocumentArchiveError("unable to write the standard document");
        }
    }
    std::filesystem::rename(staging, absolute, code);
    if (code) {
        std::error_code ignored;
        std::filesystem::remove(staging, ignored);
        throw StandardDocumentArchiveError("unable to publish the standard document");
    }

    ArchivedStandardPdf archived;
    archived.original_file_name = std::string(original_file_name);
    archived.storage_relative_path = relative.generic_string();
    archived.file_size_bytes = content.size();
    archived.sha256 = hash;
    return archived;
}

void remove_archived_standard_pdf(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& storage_relative_path,
    const std::string& sha256) noexcept {
    try {
        if (!is_safe_archive_relative_path(storage_relative_path)) return;
        const auto absolute = resolve_path_under_root(archive_root, storage_relative_path);
        if (!std::filesystem::is_regular_file(absolute)) return;

        std::string content;
        {
            // 读完就关：Windows 上文件还开着句柄就删不掉，而下面的删除失败是静默的。
            std::ifstream input(absolute, std::ios::binary);
            if (!input) return;
            content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
        // 内容对不上说明这不是我们以为的那个文件，留着比误删强。
        if (!sha256.empty() && sha256_hex(content) != sha256) return;

        std::error_code code;
        std::filesystem::remove(absolute, code);
    } catch (...) {
        // 清理失败不该影响业务结果：数据库那一侧已经改完了。
    }
}

}  // namespace bridge_report::archive
