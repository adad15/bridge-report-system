#include "bridge_report/standards/StandardPackageLoader.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <string_view>
#include <utility>

#include <json/json.h>

#include "bridge_report/auth/PasswordHash.hpp"

namespace bridge_report::standards {

namespace {

StandardIssue issue(std::string code, std::string message) {
    return {std::move(code), std::move(message)};
}

bool read_json_document(
    const std::filesystem::path& path,
    Json::Value& document,
    StandardIssue& read_issue,
    const std::string& missing_code,
    const std::string& invalid_code) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        read_issue = issue(missing_code, "规范包缺少声明的 JSON 文件。");
        return false;
    }

    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &document, &errors) || !document.isObject()) {
        read_issue = issue(invalid_code, "规范包包含无法解析的 JSON 对象。");
        return false;
    }
    return true;
}

bool is_safe_entry_file(const std::string& raw_path) {
    if (raw_path.empty()) {
        return false;
    }
    const std::filesystem::path path(raw_path);
    if (path.is_absolute() || path.has_root_name() || path.extension() != ".json") {
        return false;
    }
    for (const auto& part : path.lexically_normal()) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

std::optional<std::vector<std::string>> parse_entry_files(
    const Json::Value& manifest,
    StandardIssue& parse_issue) {
    if (!manifest.isMember("entry_files") || !manifest["entry_files"].isArray() ||
        manifest["entry_files"].empty()) {
        parse_issue = issue(
            "manifest_entry_files_invalid", "规范包清单必须声明至少一个 JSON 入口文件。");
        return std::nullopt;
    }

    std::set<std::string> unique;
    std::vector<std::string> entry_files;
    for (const auto& item : manifest["entry_files"]) {
        if (!item.isString() || !is_safe_entry_file(item.asString())) {
            parse_issue = issue(
                "manifest_entry_file_unsafe", "规范包入口文件必须是包内安全的相对 JSON 路径。");
            return std::nullopt;
        }
        const auto entry_file = std::filesystem::path(item.asString()).generic_string();
        if (!unique.insert(entry_file).second) {
            parse_issue = issue(
                "manifest_entry_file_duplicate", "规范包清单不能重复声明入口文件。");
            return std::nullopt;
        }
        entry_files.push_back(entry_file);
    }
    return entry_files;
}

std::string canonical_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    return Json::writeString(builder, value);
}

void append_digest_item(
    std::string& digest_input,
    const std::string& relative_name,
    const std::string& canonical_content) {
    digest_input += std::to_string(relative_name.size());
    digest_input.push_back(':');
    digest_input += relative_name;
    digest_input += std::to_string(canonical_content.size());
    digest_input.push_back(':');
    digest_input += canonical_content;
}

bool require_non_empty_string(
    const Json::Value& manifest,
    const char* field,
    std::string& target,
    StandardLoadResult& result) {
    if (!manifest.isMember(field) || !manifest[field].isString() ||
        manifest[field].asString().empty()) {
        result.issues.push_back(issue(
            "manifest_field_missing", "规范包清单缺少必填身份或版本字段。"));
        return false;
    }
    target = manifest[field].asString();
    return true;
}

bool parse_manifest(
    const Json::Value& document,
    StandardManifest& manifest,
    StandardLoadResult& result) {
    std::string family;
    bool valid = true;
    valid &= require_non_empty_string(document, "standard_family", family, result);
    valid &= require_non_empty_string(document, "standard_id", manifest.standard_id, result);
    valid &= require_non_empty_string(document, "standard_code", manifest.standard_code, result);
    valid &= require_non_empty_string(document, "standard_name", manifest.standard_name, result);
    valid &= require_non_empty_string(document, "official_edition", manifest.official_edition, result);
    valid &= require_non_empty_string(document, "package_version", manifest.package_version, result);
    valid &= require_non_empty_string(document, "algorithm_id", manifest.algorithm_id, result);
    valid &= require_non_empty_string(document, "effective_date", manifest.effective_date, result);
    valid &= require_non_empty_string(document, "content_checksum", manifest.content_checksum, result);
    valid &= require_non_empty_string(document, "status", manifest.status, result);

    if (!manifest.status.empty() && manifest.status != "active") {
        result.issues.push_back(issue(
            "package_status_unsupported", "规则包清单状态不受当前程序支持。"));
        valid = false;
    }

    if (!document.isMember("contract_version") || !document["contract_version"].isInt()) {
        result.issues.push_back(issue(
            "manifest_field_missing", "规范包清单缺少必填接口版本字段。"));
        valid = false;
    } else {
        manifest.contract_version = document["contract_version"].asInt();
        if (manifest.contract_version != StandardPackageLoader::supported_contract_version) {
            result.issues.push_back(issue(
                "contract_version_unsupported", "规范包接口版本不受当前程序支持。"));
            valid = false;
        }
    }

    const auto parsed_family = parse_standard_family(family);
    if (!family.empty() && !parsed_family.has_value()) {
        result.issues.push_back(issue(
            "standard_family_unsupported", "规范包 family 不受当前程序支持。"));
        valid = false;
    } else if (parsed_family.has_value()) {
        manifest.family = *parsed_family;
    }

    StandardIssue entry_issue;
    const auto entry_files = parse_entry_files(document, entry_issue);
    if (!entry_files.has_value()) {
        result.issues.push_back(std::move(entry_issue));
        valid = false;
    } else {
        manifest.entry_files = *entry_files;
    }
    return valid;
}

bool is_cross_package_reference(const std::string_view reference) {
    return reference.find("::") != std::string_view::npos ||
           reference.starts_with("package://") || reference.starts_with("standard://");
}

}  // namespace

StandardChecksumResult StandardPackageLoader::calculate_checksum(
    const std::filesystem::path& package_root) const {
    Json::Value manifest;
    StandardIssue read_issue;
    if (!read_json_document(
            package_root / "manifest.json",
            manifest,
            read_issue,
            "manifest_missing",
            "manifest_invalid_json")) {
        return {std::nullopt, std::move(read_issue)};
    }

    StandardIssue entry_issue;
    auto entry_files = parse_entry_files(manifest, entry_issue);
    if (!entry_files.has_value()) {
        return {std::nullopt, std::move(entry_issue)};
    }

    std::sort(entry_files->begin(), entry_files->end());
    Json::Value normalized_manifest = manifest;
    normalized_manifest.removeMember("content_checksum");
    normalized_manifest["entry_files"] = Json::Value(Json::arrayValue);
    for (const auto& entry_file : *entry_files) {
        normalized_manifest["entry_files"].append(entry_file);
    }

    std::string digest_input;
    append_digest_item(digest_input, "manifest.json", canonical_json(normalized_manifest));
    for (const auto& entry_file : *entry_files) {
        Json::Value document;
        if (!read_json_document(
                package_root / std::filesystem::path(entry_file),
                document,
                read_issue,
                "entry_file_missing",
                "entry_file_invalid_json")) {
            return {std::nullopt, std::move(read_issue)};
        }
        append_digest_item(digest_input, entry_file, canonical_json(document));
    }

    return {
        "sha256:" + bridge_report::auth::sha256_hex(digest_input),
        std::nullopt,
    };
}

StandardLoadResult StandardPackageLoader::load(
    const std::filesystem::path& package_root) const {
    StandardLoadResult result;
    Json::Value manifest_document;
    StandardIssue read_issue;
    if (!read_json_document(
            package_root / "manifest.json",
            manifest_document,
            read_issue,
            "manifest_missing",
            "manifest_invalid_json")) {
        result.issues.push_back(std::move(read_issue));
        return result;
    }

    StandardPackage package;
    if (!parse_manifest(manifest_document, package.manifest, result)) {
        return result;
    }

    const auto checksum_result = calculate_checksum(package_root);
    if (!checksum_result.ok()) {
        result.issues.push_back(*checksum_result.issue);
        return result;
    }
    if (*checksum_result.checksum != package.manifest.content_checksum) {
        result.issues.push_back(issue(
            "content_checksum_mismatch", "规范包内容摘要与清单声明不一致。"));
        return result;
    }

    for (const auto& entry_file : package.manifest.entry_files) {
        Json::Value document;
        if (!read_json_document(
                package_root / std::filesystem::path(entry_file),
                document,
                read_issue,
                "entry_file_missing",
                "entry_file_invalid_json")) {
            result.issues.push_back(std::move(read_issue));
            return result;
        }
        package.documents.emplace(entry_file, document);

        if (!document.isMember("definitions")) {
            continue;
        }
        if (!document["definitions"].isArray()) {
            result.issues.push_back(issue(
                "definitions_invalid", "规则文件的 definitions 必须是数组。"));
            continue;
        }
        for (const auto& definition_document : document["definitions"]) {
            if (!definition_document.isObject() || !definition_document.isMember("id") ||
                !definition_document["id"].isString() ||
                definition_document["id"].asString().empty()) {
                result.issues.push_back(issue(
                    "definition_id_missing", "规则定义必须包含非空稳定 ID。"));
                continue;
            }

            StandardDefinition definition;
            definition.id = definition_document["id"].asString();
            definition.source_file = entry_file;
            definition.payload = definition_document;
            if (definition_document.isMember("references")) {
                if (!definition_document["references"].isArray()) {
                    result.issues.push_back(issue(
                        "definition_references_invalid", "规则定义 references 必须是字符串数组。"));
                    continue;
                }
                bool references_valid = true;
                for (const auto& reference : definition_document["references"]) {
                    if (!reference.isString() || reference.asString().empty()) {
                        result.issues.push_back(issue(
                            "definition_references_invalid", "规则定义 references 必须是字符串数组。"));
                        references_valid = false;
                        break;
                    }
                    if (is_cross_package_reference(reference.asString())) {
                        result.issues.push_back(issue(
                            "definition_reference_cross_package", "规范包规则不得引用其他规则包。"));
                        references_valid = false;
                        break;
                    }
                    definition.references.push_back(reference.asString());
                }
                if (!references_valid) {
                    continue;
                }
            }

            if (!package.definitions.emplace(definition.id, std::move(definition)).second) {
                result.issues.push_back(issue(
                    "definition_id_duplicate", "规范包内的规则定义 ID 必须唯一。"));
            }
        }
    }

    for (const auto& [definition_id, definition] : package.definitions) {
        (void)definition_id;
        for (const auto& reference : definition.references) {
            if (!package.definitions.contains(reference)) {
                result.issues.push_back(issue(
                    "definition_reference_dangling", "规范包包含无法解析的规则引用。"));
            }
        }
    }

    if (result.issues.empty()) {
        result.package = std::move(package);
    }
    return result;
}

std::vector<std::filesystem::path> StandardPackageLoader::discover(
    const std::filesystem::path& standards_root) const {
    std::vector<std::filesystem::path> packages;
    std::error_code error;
    if (!std::filesystem::is_directory(standards_root, error) || error) {
        return packages;
    }

    std::filesystem::recursive_directory_iterator iterator(
        standards_root,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error) && !error &&
            iterator->path().filename() == "manifest.json") {
            packages.push_back(iterator->path().parent_path());
        }
        iterator.increment(error);
    }
    std::sort(packages.begin(), packages.end());
    return packages;
}

}  // namespace bridge_report::standards
