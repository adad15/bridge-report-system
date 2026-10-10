#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <regex>
#include <sstream>
#include <optional>
#include <string_view>
#include <cctype>
#include <string>

#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include "bridge_report/http/Cors.hpp"

namespace bridge_report::http {

using HttpCallback = std::function<void(const drogon::HttpResponsePtr&)>;

/**
 * @brief 先用正则校验路径参数，避免把非法 uuid 引发的 SQL 异常与数据库故障混为一谈。
 *
 * 供 ReviewRoutes.cpp / ImportConfirmRoutes.cpp 共用。
 */
/**
 * @brief 解析 `If-Match: "draft-<version>"`。缺失或格式不对时返回 nullopt。
 *
 * 用标准头而不是塞进请求体，是为了不把并发元数据混进 5.0 合同：
 * `saveReviewDraft` 的请求体必须仍然是一份纯粹的 BridgeAnnualInspectionData（§11.1）。
 *
 * 草稿写入三条路径（普通保存、手工新增病害、照片增删）共用同一个
 * draft_version 边界，因此解析也只能有一份（§8.0）。
 */
inline std::optional<int> parse_if_match_draft_version(
    const drogon::HttpRequestPtr& request) {
    auto value = request->getHeader("if-match");
    if (value.empty()) value = request->getHeader("If-Match");
    if (value.size() < 3) return std::nullopt;
    if (value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    constexpr std::string_view prefix = "draft-";
    if (value.rfind(prefix, 0) != 0) return std::nullopt;
    const auto digits = value.substr(prefix.size());
    if (digits.empty()) return std::nullopt;
    for (const char character : digits) {
        if (!std::isdigit(static_cast<unsigned char>(character))) return std::nullopt;
    }
    try {
        return std::stoi(digits);
    } catch (...) {
        return std::nullopt;
    }
}

/// 把新的草稿版本放进 ETag，让客户端下一次写能带对 If-Match。
inline void set_draft_version_etag(const drogon::HttpResponsePtr& response, int version) {
    response->addHeader("ETag", "\"draft-" + std::to_string(version) + "\"");
}

inline bool is_valid_uuid(const std::string& value) {
    static const std::regex uuid_pattern(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
    );
    return std::regex_match(value, uuid_pattern);
}

inline Json::Value make_error_body(const std::string& code, const std::string& message) {
    Json::Value body;
    body["code"] = code;
    body["message"] = message;
    return body;
}

inline void respond_json(
    const HttpCallback& callback,
    const Json::Value& body,
    drogon::HttpStatusCode status = drogon::k200OK
) {
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(status);
    apply_local_dev_cors_headers(response);
    callback(response);
}

inline void respond_db_unavailable(const HttpCallback& callback) {
    respond_json(
        callback,
        make_error_body("db_unavailable", "数据库暂不可用，请稍后重试。"),
        drogon::k503ServiceUnavailable
    );
}

inline void respond_import_record_not_found(const HttpCallback& callback) {
    respond_json(
        callback,
        make_error_body("import_record_not_found", "指定的导入记录不存在"),
        drogon::k404NotFound
    );
}

// 401：未登录 / 会话过期。前端 apiClient 对该状态码统一清空本地会话回登录页。
inline void respond_unauthorized(const HttpCallback& callback) {
    respond_json(
        callback,
        make_error_body("auth_required", "请先登录。"),
        drogon::k401Unauthorized
    );
}

// 403：已登录但角色不够（如普通账号请求管理员专属操作）。
inline void respond_forbidden(const HttpCallback& callback) {
    respond_json(
        callback,
        make_error_body("forbidden", "当前账号无权执行该操作。"),
        drogon::k403Forbidden
    );
}

// parsed_result_json 存储为 jsonb 文本；解析失败（理论上不应发生，防御式处理）时退化为空对象，
// 使下游纯函数（build_review_statistics / build_preflight_report 等）仍能得到全 0 统计 /
// 空数据视图而不是崩溃。供 ReviewRoutes.cpp / ImportConfirmRoutes.cpp 共用。
inline Json::Value parse_parsed_result_json(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return Json::Value(Json::objectValue);
    }
    return root;
}

/**
 * @brief 文件响应的 Content-Disposition，中文文件名走 RFC 5987 的 filename*。
 *
 * 只写 `filename=` 会被大多数浏览器按 latin-1 解释，`百股大桥定期检测报告.docx`
 * 存下来就是一串乱码；两个都写，老客户端用前者、新客户端用后者。前一个退化成
 * ASCII 占位，真正的名字由 filename* 给出。
 *
 * kind 为 "attachment"（下载）或 "inline"（在浏览器里直接打开，例如 iframe 里的 PDF）。
 *
 * 注意跨域时还要把 Content-Disposition 放进 Access-Control-Expose-Headers，
 * 否则前端用 fetch 取 blob 根本读不到这个头（见 Cors.cpp）。
 */
inline std::string content_disposition(
    std::string_view kind, const std::string& filename, const std::string& ascii_fallback = "download") {
    std::ostringstream encoded;
    encoded << std::hex << std::uppercase;
    for (const unsigned char character : filename) {
        const bool unreserved =
            (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == '.' || character == '~';
        if (unreserved) {
            encoded << static_cast<char>(character);
        } else {
            encoded << '%' << (character < 0x10 ? "0" : "") << static_cast<int>(character);
        }
    }
    return std::string(kind) + "; filename=\"" + ascii_fallback + "\"; filename*=UTF-8''" + encoded.str();
}

inline std::string attachment_disposition(
    const std::string& filename, const std::string& ascii_fallback = "download") {
    return content_disposition("attachment", filename, ascii_fallback);
}

/// 单段 Range 请求的解析结论（RFC 9110 §14）。
struct ByteRange {
    enum class Kind {
        Whole,          ///< 没带 Range、写法不认识或一次要多段：回整份文件（200），RFC 允许服务器这样做。
        Partial,        ///< 回 [offset, offset + length) 这一段（206）。
        Unsatisfiable,  ///< 起点在文件末尾之后：回 416，带 `Content-Range: bytes */<size>`。
    };
    Kind kind{Kind::Whole};
    std::uint64_t offset{0};
    std::uint64_t length{0};
};

/**
 * @brief 解析 `Range: bytes=a-b`、`bytes=a-`、`bytes=-n`，只认单段。
 *
 * 浏览器的 PDF 阅读器靠它边下边看：先取文件尾的交叉引用表，再按页取。
 * drogon 的 newFileResponse 不会替我们看这个头，得自己解析后把 offset / length 传进去。
 */
inline ByteRange parse_byte_range(std::string_view header, std::uint64_t size) {
    const auto trim = [](std::string_view text) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
        return text;
    };
    const auto parse_number = [](std::string_view text) -> std::optional<std::uint64_t> {
        if (text.empty()) return std::nullopt;
        std::uint64_t value = 0;
        const auto* end = text.data() + text.size();
        const auto [stop, error] = std::from_chars(text.data(), end, value);
        if (error != std::errc{} || stop != end) return std::nullopt;
        return value;
    };

    header = trim(header);
    const auto equals = header.find('=');
    if (equals == std::string_view::npos) return {};
    std::string unit(trim(header.substr(0, equals)));
    std::transform(unit.begin(), unit.end(), unit.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    const auto spec = trim(header.substr(equals + 1));
    if (unit != "bytes" || spec.find(',') != std::string_view::npos) return {};
    const auto dash = spec.find('-');
    if (dash == std::string_view::npos) return {};
    const auto first_text = trim(spec.substr(0, dash));
    const auto last_text = trim(spec.substr(dash + 1));
    const ByteRange unsatisfiable{ByteRange::Kind::Unsatisfiable};

    if (first_text.empty()) {  // bytes=-n：最后 n 个字节
        const auto suffix = parse_number(last_text);
        if (!suffix) return {};
        if (*suffix == 0 || size == 0) return unsatisfiable;
        const auto length = (std::min)(*suffix, size);
        return {ByteRange::Kind::Partial, size - length, length};
    }
    const auto first = parse_number(first_text);
    if (!first) return {};
    std::uint64_t last = size == 0 ? 0 : size - 1;
    if (!last_text.empty()) {
        const auto requested = parse_number(last_text);
        if (!requested || *requested < *first) return {};
        last = (std::min)(*requested, last);
    }
    if (*first >= size) return unsatisfiable;
    return {ByteRange::Kind::Partial, *first, last - *first + 1};
}

// OPTIONS 预检处理器：只回 CORS 头，供两个路由文件登记各自路径时共用。
inline void register_options_handler(const std::string& path) {
    drogon::app().registerHandler(
        path,
        [](const drogon::HttpRequestPtr&, HttpCallback&& callback) {
            auto response = drogon::HttpResponse::newHttpResponse();
            apply_local_dev_cors_headers(response);
            callback(response);
        },
        {drogon::Options, "drogon::HttpOptionsMiddleware"}
    );
}

}  // 命名空间 bridge_report::http
