#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bridge_report::archive {

class StandardDocumentArchiveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// 扩展名不是 .pdf，或文件头不是 `%PDF-`。调用方要把它和写盘失败映射成不同的 HTTP 状态。
class StandardDocumentNotPdfError : public StandardDocumentArchiveError {
public:
    using StandardDocumentArchiveError::StandardDocumentArchiveError;
};

/// 空文件。
class StandardDocumentEmptyError : public StandardDocumentArchiveError {
public:
    using StandardDocumentArchiveError::StandardDocumentArchiveError;
};

struct ArchivedStandardPdf {
    std::string original_file_name;
    std::string storage_relative_path;
    std::uintmax_t file_size_bytes{0};
    std::string sha256;
};

/// 扩展名（不分大小写）是 .pdf，而且内容以 `%PDF-` 开头。
bool looks_like_pdf(std::string_view content, std::string_view original_file_name);

/// `standards/<规范 id>/<文件名>`，全是 ASCII：窄字符 API 遇到中文路径会建出乱码目录。
std::filesystem::path build_standard_document_relative_path(
    std::string_view document_id, std::string_view file_name);

/**
 * @brief 把一份规范原文写进归档。
 *
 * 校验全在写盘之前完成；先写 `.uploading` 再改名，任何失败都不留下半个文件。
 * 每次都用新的 UUID 文件名：替换原文时新旧文件互不覆盖，提交不确定时两份都还在。
 *
 * @throws StandardDocumentEmptyError 内容为空。
 * @throws StandardDocumentNotPdfError 不是 PDF。
 * @throws StandardDocumentArchiveError 写盘失败。
 */
ArchivedStandardPdf archive_standard_pdf(
    std::string_view content,
    std::string_view original_file_name,
    const std::filesystem::path& archive_root,
    std::string_view document_id);

/// 删除已归档的原文；SHA-256 对不上就原样留着，宁可留垃圾也不误删。
void remove_archived_standard_pdf(
    const std::filesystem::path& archive_root,
    const std::filesystem::path& storage_relative_path,
    const std::string& sha256) noexcept;

}  // namespace bridge_report::archive
