#pragma once

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
