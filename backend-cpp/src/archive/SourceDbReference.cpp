#include "bridge_report/archive/SourceDbReference.hpp"

#include <array>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include "bridge_report/auth/PasswordHash.hpp"

#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>

namespace bridge_report::archive {
namespace {

//: SQLite 库的前 16 字节固定是这串字面量（含结尾的 \0）。
constexpr std::string_view kSqliteMagic{"SQLite format 3\0", 16};

}  // namespace

std::filesystem::path default_source_db_path() {
    const auto* roaming = std::getenv("APPDATA");
    if (roaming == nullptr || roaming[0] == '\0') return {};
    return path_from_utf8(roaming) / "datacheck.hitek.com" / "databases" /
        "https_bridge.ilis.cn_0" / "1";
}

std::filesystem::path path_from_utf8(const std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

SourceDbValidationResult validate_source_db(const SourceDbReference& reference) {
    SourceDbValidationResult result;
    if (reference.task_id.empty()) {
        result.error = SourceDbValidationError::TaskIdMissing;
        return result;
    }
    if (reference.source_db_path.empty()) {
        result.error = SourceDbValidationError::PathMissing;
        return result;
    }
    std::filesystem::path path;
    try { path = path_from_utf8(reference.source_db_path); }
    catch (const std::exception&) {
        result.error = SourceDbValidationError::PathMissing;
        return result;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        result.error = SourceDbValidationError::PathMissing;
        return result;
    }

    std::ifstream input(path, std::ios::binary);
    std::array<char, 16> header{};
    if (!input || !input.read(header.data(), static_cast<std::streamsize>(header.size()))) {
        result.error = SourceDbValidationError::NotSqlite;
        return result;
    }
    if (std::string_view(header.data(), header.size()) != kSqliteMagic) {
        result.error = SourceDbValidationError::NotSqlite;
        return result;
    }

    // 文件名可能含中文；用 u8string 取回再转 UTF-8，不经过当前代码页。
    const auto name = path.filename().u8string();
    result.original_file_name.assign(name.begin(), name.end());
    return result;
}

std::string encode_source_db_reference(const SourceDbReference& reference) {
    Json::Value document;
    document["source_db_path"] = reference.source_db_path;
    document["task_id"] = reference.task_id;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    return Json::writeString(builder, document);
}

SourceDbReference decode_source_db_reference(const std::string_view content) {
    Json::Value document;
    Json::CharReaderBuilder builder;
    std::string errors;
    const auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
    if (!reader->parse(content.data(), content.data() + content.size(), &document, &errors)
        || !document.isObject()) {
        throw std::invalid_argument("source database reference is not a JSON object");
    }
    if (!document["source_db_path"].isString() || !document["task_id"].isString()) {
        throw std::invalid_argument("source database reference is missing its path or task id");
    }
    return {document["source_db_path"].asString(), document["task_id"].asString()};
}

WordInputMetadata describe_source_db_reference(
    const std::string_view content, const std::string& original_file_name) {
    WordInputMetadata metadata;
    metadata.original_file_name = original_file_name;
    metadata.file_extension = std::string(kSourceDbReferenceExtension);
    metadata.file_size_bytes = content.size();
    metadata.sha256 = auth::sha256_hex(std::string(content));
    return metadata;
}

}  // namespace bridge_report::archive
