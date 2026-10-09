#include "bridge_report/standards/StandardDocumentModels.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <regex>

namespace bridge_report::standards {
namespace {

constexpr std::size_t kNameMax = 100;
constexpr std::size_t kCategoryMax = 30;
constexpr std::size_t kLabelMax = 40;
constexpr std::size_t kCodeMax = 60;
constexpr std::size_t kNoteMax = 500;
constexpr std::size_t kTextMax = 200;

/// 按字符计长度：UTF-8 里只数非续字节。
std::size_t character_count(const std::string& value) {
    return static_cast<std::size_t>(std::count_if(value.begin(), value.end(), [](char character) {
        return (static_cast<unsigned char>(character) & 0xC0) != 0x80;
    }));
}

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

void add_issue(std::vector<StandardInputIssue>& issues, const std::string& prefix,
               const std::string& field, const std::string& message) {
    issues.push_back({prefix + field, message});
}

std::string object_path(const std::string& prefix) {
    return prefix.empty() ? "body" : prefix.substr(0, prefix.size() - 1);
}

std::string length_message(const std::string& label, std::size_t max) {
    return label + "不能超过 " + std::to_string(max) + " 个字。";
}

/// 必填文本：缺失、不是字符串、去空白后为空、超长都记问题。
std::string required_text(const Json::Value& body, const char* field, std::size_t max,
                          const std::string& label, const std::string& prefix,
                          std::vector<StandardInputIssue>& issues) {
    const auto& value = body[field];
    if (!value.isString() || trim(value.asString()).empty()) {
        add_issue(issues, prefix, field, label + "不能为空。");
        return "";
    }
    const auto text = trim(value.asString());
    if (character_count(text) > max) add_issue(issues, prefix, field, length_message(label, max));
    return text;
}

/// 可选文本：缺失、null、空串都当没填；不是字符串或超长记问题。
std::optional<std::string> optional_text(const Json::Value& body, const char* field, std::size_t max,
                                         const std::string& label, const std::string& prefix,
                                         std::vector<StandardInputIssue>& issues) {
    const auto& value = body[field];
    if (value.isNull()) return std::nullopt;
    if (!value.isString()) {
        add_issue(issues, prefix, field, label + "必须是文本。");
        return std::nullopt;
    }
    const auto text = trim(value.asString());
    if (text.empty()) return std::nullopt;
    if (character_count(text) > max) add_issue(issues, prefix, field, length_message(label, max));
    return text;
}

std::optional<std::string> optional_date(const Json::Value& body, const char* field,
                                         const std::string& label, const std::string& prefix,
                                         std::vector<StandardInputIssue>& issues) {
    const auto& value = body[field];
    if (value.isNull()) return std::nullopt;
    const auto text = value.isString() ? trim(value.asString()) : std::string("invalid");
    if (text.empty()) return std::nullopt;
    if (!is_valid_iso_date(text)) {
        add_issue(issues, prefix, field, label + "必须是 YYYY-MM-DD 格式的日期。");
        return std::nullopt;
    }
    return text;
}

Json::Value nullable(const std::optional<std::string>& value) {
    return value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

}  // namespace

std::optional<StandardVersionKind> parse_standard_version_kind(const std::string& code) {
    if (code == "official") return StandardVersionKind::official;
    if (code == "draft_comment") return StandardVersionKind::draft_comment;
    if (code == "draft_approval") return StandardVersionKind::draft_approval;
    return std::nullopt;
}

const char* standard_version_kind_code(StandardVersionKind kind) {
    switch (kind) {
        case StandardVersionKind::official: return "official";
        case StandardVersionKind::draft_comment: return "draft_comment";
        case StandardVersionKind::draft_approval: return "draft_approval";
    }
    return "official";
}

const char* standard_version_status_code(StandardVersionStatus status) {
    switch (status) {
        case StandardVersionStatus::current: return "current";
        case StandardVersionStatus::upcoming: return "upcoming";
        case StandardVersionStatus::withdrawn: return "withdrawn";
        case StandardVersionStatus::draft_comment: return "draft_comment";
        case StandardVersionStatus::draft_approval: return "draft_approval";
    }
    return "current";
}

std::string standard_version_date(const StandardVersionRecord& version) {
    if (version.effective_date.has_value()) return *version.effective_date;
    if (version.publish_date.has_value()) return *version.publish_date;
    return version.created_date;
}

std::vector<DerivedVersionState> derive_version_states(
    const std::vector<StandardVersionRecord>& versions, const std::string& today) {
    std::vector<DerivedVersionState> states(versions.size());

    // 正式版按实施日期排成一条链：每个版本的后继就是替代它的那个（§6.5）。
    std::vector<std::size_t> chain;
    for (std::size_t index = 0; index < versions.size(); ++index) {
        if (versions[index].kind == StandardVersionKind::official) chain.push_back(index);
    }
    std::sort(chain.begin(), chain.end(), [&](std::size_t left, std::size_t right) {
        return versions[left].effective_date.value_or("") < versions[right].effective_date.value_or("");
    });

    for (std::size_t position = 0; position < chain.size(); ++position) {
        const auto& version = versions[chain[position]];
        auto& state = states[chain[position]];
        const bool has_next = position + 1 < chain.size();
        if (has_next) state.replaced_by_version_id = versions[chain[position + 1]].id;
        if (position > 0) state.replaces_version_id = versions[chain[position - 1]].id;

        if (version.withdrawn_date.has_value()) {
            state.withdrawn_date = version.withdrawn_date;
            state.withdrawn_date_source = "manual";
        } else if (has_next) {
            state.withdrawn_date = versions[chain[position + 1]].effective_date;
            state.withdrawn_date_source = "replacement";
        }

        if (state.withdrawn_date.has_value() && *state.withdrawn_date <= today) {
            state.status = StandardVersionStatus::withdrawn;
        } else if (version.effective_date.value_or("") <= today) {
            state.status = StandardVersionStatus::current;
        } else {
            state.status = StandardVersionStatus::upcoming;
        }
    }

    for (std::size_t index = 0; index < versions.size(); ++index) {
        if (versions[index].kind == StandardVersionKind::draft_comment) {
            states[index].status = StandardVersionStatus::draft_comment;
        } else if (versions[index].kind == StandardVersionKind::draft_approval) {
            states[index].status = StandardVersionStatus::draft_approval;
        }
    }
    return states;
}

std::optional<std::string> default_standard_version_id(
    const std::vector<StandardVersionRecord>& versions,
    const std::vector<DerivedVersionState>& states) {
    for (std::size_t index = 0; index < versions.size(); ++index) {
        if (states[index].status == StandardVersionStatus::current) return versions[index].id;
    }
    std::optional<std::size_t> latest;
    for (std::size_t index = 0; index < versions.size(); ++index) {
        if (!latest.has_value() ||
            standard_version_date(versions[index]) > standard_version_date(versions[*latest])) {
            latest = index;
        }
    }
    if (!latest.has_value()) return std::nullopt;
    return versions[*latest].id;
}

Json::Value standard_document_json(const StandardDocumentRecord& document, const std::string& today) {
    const auto states = derive_version_states(document.versions, today);
    Json::Value body;
    body["id"] = document.id;
    body["name"] = document.name;
    body["english_name"] = nullable(document.english_name);
    body["category"] = document.category;
    body["default_version_id"] = nullable(default_standard_version_id(document.versions, states));
    body["versions"] = Json::Value(Json::arrayValue);
    for (std::size_t index = 0; index < document.versions.size(); ++index) {
        const auto& version = document.versions[index];
        const auto& state = states[index];
        Json::Value item;
        item["id"] = version.id;
        item["label"] = version.label;
        item["code"] = version.code;
        item["kind"] = standard_version_kind_code(version.kind);
        item["status"] = standard_version_status_code(state.status);
        item["nature"] = nullable(version.nature);
        item["issuer"] = nullable(version.issuer);
        item["editor"] = nullable(version.editor);
        item["publish_date"] = nullable(version.publish_date);
        item["effective_date"] = nullable(version.effective_date);
        item["withdrawn_date"] = nullable(state.withdrawn_date);
        item["withdrawn_date_source"] = nullable(state.withdrawn_date_source);
        item["announcement"] = nullable(version.announcement);
        item["source"] = nullable(version.source);
        item["note"] = nullable(version.note);
        item["replaced_by_version_id"] = nullable(state.replaced_by_version_id);
        item["replaces_version_id"] = nullable(state.replaces_version_id);
        Json::Value file;
        file["original_name"] = version.original_file_name;
        file["size_bytes"] = static_cast<Json::UInt64>(version.file_size_bytes);
        item["file"] = file;
        item["content_path"] = "/api/standard-document-versions/" + version.id + "/content";
        item["uploaded_by"] = nullable(version.uploaded_by);
        item["uploaded_at"] = version.uploaded_at;
        item["updated_at"] = version.updated_at;
        body["versions"].append(item);
    }
    return body;
}

std::string local_today() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[11]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local);
    return buffer;
}

bool is_valid_iso_date(const std::string& value) {
    static const std::regex pattern("^[0-9]{4}-[0-9]{2}-[0-9]{2}$");
    if (!std::regex_match(value, pattern)) return false;
    const std::chrono::year year{std::stoi(value.substr(0, 4))};
    const std::chrono::month month{static_cast<unsigned>(std::stoi(value.substr(5, 2)))};
    const std::chrono::day day{static_cast<unsigned>(std::stoi(value.substr(8, 2)))};
    return std::chrono::year_month_day{year, month, day}.ok();
}

std::vector<StandardInputIssue> parse_standard_document_input(
    const Json::Value& body, StandardDocumentInput& input, const std::string& prefix) {
    std::vector<StandardInputIssue> issues;
    if (!body.isObject()) {
        issues.push_back({object_path(prefix), "规范信息必须是 JSON 对象。"});
        return issues;
    }
    input.name = required_text(body, "name", kNameMax, "规范名称", prefix, issues);
    input.english_name = optional_text(body, "english_name", kTextMax, "英文名称", prefix, issues);
    input.category = required_text(body, "category", kCategoryMax, "分类", prefix, issues);
    return issues;
}

std::vector<StandardInputIssue> parse_standard_version_input(
    const Json::Value& body, StandardVersionInput& input, const std::string& prefix) {
    std::vector<StandardInputIssue> issues;
    if (!body.isObject()) {
        issues.push_back({object_path(prefix), "版本信息必须是 JSON 对象。"});
        return issues;
    }
    input.label = required_text(body, "label", kLabelMax, "版本名称", prefix, issues);
    input.code = required_text(body, "code", kCodeMax, "标准编号", prefix, issues);
    const auto kind = body["kind"].isString()
        ? parse_standard_version_kind(body["kind"].asString())
        : std::nullopt;
    if (kind.has_value()) {
        input.kind = *kind;
    } else {
        add_issue(issues, prefix, "kind", "稿件类型必须是正式版、征求意见稿或报批稿。");
    }
    input.nature = optional_text(body, "nature", kTextMax, "标准性质", prefix, issues);
    input.issuer = optional_text(body, "issuer", kTextMax, "发布部门", prefix, issues);
    input.editor = optional_text(body, "editor", kTextMax, "主编单位", prefix, issues);
    input.publish_date = optional_date(body, "publish_date", "发布日期", prefix, issues);
    input.effective_date = optional_date(body, "effective_date", "实施日期", prefix, issues);
    input.withdrawn_date = optional_date(body, "withdrawn_date", "废止日期", prefix, issues);
    input.announcement = optional_text(body, "announcement", kTextMax, "公告文号", prefix, issues);
    input.source = optional_text(body, "source", kTextMax, "原文来源", prefix, issues);
    input.note = optional_text(body, "note", kNoteMax, "备注", prefix, issues);

    const auto has_issue = [&](const char* field) {
        return std::any_of(issues.begin(), issues.end(), [&](const StandardInputIssue& issue) {
            return issue.path == prefix + field;
        });
    };
    if (kind == StandardVersionKind::official) {
        if (!input.effective_date.has_value() && !has_issue("effective_date")) {
            add_issue(issues, prefix, "effective_date", "正式版必须填写实施日期。");
        }
        if (input.withdrawn_date.has_value() && input.effective_date.has_value() &&
            *input.withdrawn_date < *input.effective_date) {
            add_issue(issues, prefix, "withdrawn_date", "废止日期不能早于实施日期。");
        }
    } else if (kind.has_value()) {
        if (input.effective_date.has_value()) {
            add_issue(issues, prefix, "effective_date", "征求意见稿、报批稿不填实施日期。");
        }
        if (input.withdrawn_date.has_value()) {
            add_issue(issues, prefix, "withdrawn_date", "征求意见稿、报批稿不填废止日期。");
        }
    }
    return issues;
}

Json::Value standard_input_issues_json(const std::vector<StandardInputIssue>& issues) {
    Json::Value array(Json::arrayValue);
    for (const auto& issue : issues) {
        Json::Value item;
        item["path"] = issue.path;
        item["message"] = issue.message;
        array.append(item);
    }
    return array;
}

std::string standard_pdf_file_name(const std::string& code, const std::string& document_name) {
    std::string result;
    // 逐字节处理是安全的：UTF-8 多字节序列里的每个字节都 ≥ 0x80，不会撞上下面这些 ASCII。
    for (const char character : code + " " + document_name) {
        if (character == '/') {
            result += ' ';
        } else if (character == '\\' || character == ':' || character == '*' || character == '?' ||
                   character == '"' || character == '<' || character == '>' || character == '|') {
            result += '-';
        } else {
            result += character;
        }
    }
    return result + ".pdf";
}

}  // namespace bridge_report::standards
