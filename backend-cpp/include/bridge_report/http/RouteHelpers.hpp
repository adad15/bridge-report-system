#pragma once

#include <functional>
#include <regex>
#include <sstream>
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
