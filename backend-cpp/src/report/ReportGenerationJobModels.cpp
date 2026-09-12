#include "bridge_report/report/ReportGenerationJobModels.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace bridge_report::report {
namespace {

constexpr std::array<std::pair<JobStatus, std::string_view>, 8> kStatusText{{
    {JobStatus::Queued, "queued"},
    {JobStatus::ValidatingData, "validating_data"},
    {JobStatus::AssemblingDocx, "assembling_docx"},
    {JobStatus::UpdatingFields, "updating_fields"},
    {JobStatus::ValidatingDocx, "validating_docx"},
    {JobStatus::Ready, "ready"},
    {JobStatus::Failed, "failed"},
    {JobStatus::Expired, "expired"},
}};

void put(Json::Value& json, const char* key, const std::optional<std::string>& value) {
    json[key] = value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

/// Windows 文件名里不能出现的字符，外加控制字符。
///
/// 只清理文件名这一层，不动文档内部的业务值——路线名里真有个斜杠时，报告正文里
/// 仍要原样印出来（设计 §20）。
bool is_illegal_filename_char(unsigned char character) {
    if (character < 0x20) return true;
    switch (character) {
        case '<': case '>': case ':': case '"':
        case '/': case '\\': case '|': case '?': case '*':
            return true;
        default:
            return false;
    }
}

std::string sanitize(const std::string& value) {
    std::string cleaned;
    cleaned.reserve(value.size());
    for (const char character : value) {
        if (!is_illegal_filename_char(static_cast<unsigned char>(character))) {
            cleaned.push_back(character);
        }
    }
    // 首尾的空白和点会被 Windows 自己截掉，留着只会让实际文件名与记录对不上。
    const auto first = cleaned.find_first_not_of(" \t.");
    if (first == std::string::npos) return {};
    const auto last = cleaned.find_last_not_of(" \t.");
    return cleaned.substr(first, last - first + 1);
}

void append(std::string& name, const std::optional<std::string>& part) {
    if (!part.has_value()) return;
    const auto cleaned = sanitize(*part);
    if (cleaned.empty()) return;
    name += cleaned;
}

}  // namespace

std::string job_status_text(JobStatus status) {
    for (const auto& [value, text] : kStatusText) {
        if (value == status) return std::string(text);
    }
    return "queued";
}

std::optional<JobStatus> job_status_from_text(const std::string& text) {
    for (const auto& [value, candidate] : kStatusText) {
        if (candidate == text) return value;
    }
    return std::nullopt;
}

bool job_is_running(JobStatus status) {
    switch (status) {
        case JobStatus::Queued:
        case JobStatus::ValidatingData:
        case JobStatus::AssemblingDocx:
        case JobStatus::UpdatingFields:
        case JobStatus::ValidatingDocx:
            return true;
        default:
            return false;
    }
}

bool job_is_final(JobStatus status) { return !job_is_running(status); }

bool ReportGenerationJob::can_download() const {
    return status == JobStatus::Ready && temporary_file_path.has_value();
}

Json::Value ReportGenerationJob::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["inspection_year_id"] = inspection_year_id;
    json["requested_by_user_id"] = requested_by_user_id;
    json["status"] = job_status_text(status);
    json["is_running"] = job_is_running(status);
    json["progress"] = progress;
    put(json, "template_id", template_id);
    put(json, "template_checksum", template_checksum);
    put(json, "error_code", error_code);
    put(json, "error_message", error_message);
    json["created_at"] = created_at;
    put(json, "finished_at", finished_at);
    put(json, "expires_at", expires_at);
    put(json, "download_filename", download_filename);
    // 临时路径不出接口：它是服务器上的绝对路径，对前端没有用，泄露出去只会多一条
    // 攻击面。下载一律走任务 ID（设计 §22 最后一条）。
    json["can_download"] = can_download();
    return json;
}

std::string build_report_filename(const ReportFilenameParts& parts) {
    std::string name;
    append(name, parts.report_number);
    append(name, parts.administrative_region);
    append(name, parts.route_code);
    append(name, parts.route_name);
    append(name, parts.bridge_name);
    name += "定期检测报告";
    if (parts.overall_grade.has_value()) {
        const auto grade = sanitize(*parts.overall_grade);
        if (!grade.empty()) {
            name += "（" + grade + "）";
        }
    }
    name += ".docx";
    return name;
}

}  // namespace bridge_report::report
