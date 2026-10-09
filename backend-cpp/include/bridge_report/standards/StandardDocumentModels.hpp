#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

namespace bridge_report::standards {

/*
 * 规范原文库的领域模型（设计 docs/superpowers/specs/2026-10-09-standard-documents-library-design.md §6）。
 *
 * 和同目录下的规则包（StandardModels 等）是两回事：规则包是本系统的结构化计算规则，
 * 这里是规范原文 PDF 及其版本。日期一律用 `YYYY-MM-DD` 字符串，字典序就是时间序。
 */

enum class StandardVersionKind { official, draft_comment, draft_approval };

/// 推导出来的状态，不存库（§6.4）。
enum class StandardVersionStatus { current, upcoming, withdrawn, draft_comment, draft_approval };

std::optional<StandardVersionKind> parse_standard_version_kind(const std::string& code);
const char* standard_version_kind_code(StandardVersionKind kind);
const char* standard_version_status_code(StandardVersionStatus status);

struct StandardVersionRecord {
    std::string id;
    std::string document_id;
    std::string label;
    std::string code;
    StandardVersionKind kind{StandardVersionKind::official};
    std::optional<std::string> nature;
    std::optional<std::string> issuer;
    std::optional<std::string> editor;
    std::optional<std::string> publish_date;
    std::optional<std::string> effective_date;
    /// 手工填写的废止日期；被后续版本替代的那种不存在这里（§6.5）。
    std::optional<std::string> withdrawn_date;
    std::optional<std::string> announcement;
    std::optional<std::string> source;
    std::optional<std::string> note;
    std::string archived_file_id;
    std::string original_file_name;
    std::string storage_relative_path;
    std::string file_hash;
    std::uintmax_t file_size_bytes{0};
    /// 上传人的显示名。
    std::optional<std::string> uploaded_by;
    std::string uploaded_at;
    std::string updated_at;
    /// 记录创建那天，版本日期的最后兜底（§6.6）。
    std::string created_date;
};

struct StandardDocumentRecord {
    std::string id;
    std::string name;
    std::optional<std::string> english_name;
    std::string category;
    /// 按版本日期从新到旧。
    std::vector<StandardVersionRecord> versions;
};

/// 一个版本推导出来的状态与替代关系，与输入的版本一一对应、顺序相同。
struct DerivedVersionState {
    StandardVersionStatus status{StandardVersionStatus::current};
    /// 生效的废止日期：手工值优先，否则取紧随其后那个正式版的实施日期。
    std::optional<std::string> withdrawn_date;
    /// "manual" 或 "replacement"；没有废止日期时为空。
    std::optional<std::string> withdrawn_date_source;
    std::optional<std::string> replaced_by_version_id;
    std::optional<std::string> replaces_version_id;
};

/// 版本日期 = 实施日期，没有就用发布日期，再没有就用创建日期；所有「从新到旧」都按它排。
std::string standard_version_date(const StandardVersionRecord& version);

std::vector<DerivedVersionState> derive_version_states(
    const std::vector<StandardVersionRecord>& versions, const std::string& today);

/// 现行版本；没有就取版本日期最晚的；没有版本时为空。
std::optional<std::string> default_standard_version_id(
    const std::vector<StandardVersionRecord>& versions,
    const std::vector<DerivedVersionState>& states);

Json::Value standard_document_json(const StandardDocumentRecord& document, const std::string& today);

/// 后端机器的本地日期，作状态推导的「今天」。
std::string local_today();

bool is_valid_iso_date(const std::string& value);

struct StandardDocumentInput {
    std::string name;
    std::optional<std::string> english_name;
    std::string category;
};

struct StandardVersionInput {
    std::string label;
    std::string code;
    StandardVersionKind kind{StandardVersionKind::official};
    std::optional<std::string> nature;
    std::optional<std::string> issuer;
    std::optional<std::string> editor;
    std::optional<std::string> publish_date;
    std::optional<std::string> effective_date;
    std::optional<std::string> withdrawn_date;
    std::optional<std::string> announcement;
    std::optional<std::string> source;
    std::optional<std::string> note;
};

struct StandardInputIssue {
    std::string path;
    std::string message;
};

/// 解析并校验规范信息；问题列表为空才算通过。每条问题的 path 前面拼上 prefix。
std::vector<StandardInputIssue> parse_standard_document_input(
    const Json::Value& body, StandardDocumentInput& input, const std::string& prefix = "");

/// 解析并校验版本信息，规则见设计 §7.4。
std::vector<StandardInputIssue> parse_standard_version_input(
    const Json::Value& body, StandardVersionInput& input, const std::string& prefix = "");

Json::Value standard_input_issues_json(const std::vector<StandardInputIssue>& issues);

/// 下载文件名「{标准编号} {规范名称}.pdf」：`/` 换成空格，其他 Windows 禁用字符换成 `-`。
std::string standard_pdf_file_name(const std::string& code, const std::string& document_name);

}  // namespace bridge_report::standards
