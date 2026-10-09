#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/HttpTypes.h>
#include <drogon/orm/DbClient.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/StandardDocumentRepository.hpp"
#include "bridge_report/standards/StandardDocumentModels.hpp"

namespace bridge_report::http {

/// 新增规范的 metadata：`{"document": {...}, "version": {...}}`，问题路径带 document./version. 前缀。
std::vector<standards::StandardInputIssue> parse_create_standard_metadata(
    const std::string& text,
    standards::StandardDocumentInput& document,
    standards::StandardVersionInput& version);

/// 上传新版本的 metadata：版本字段本身。
std::vector<standards::StandardInputIssue> parse_standard_version_metadata(
    const std::string& text, standards::StandardVersionInput& version);

struct StandardWriteError {
    std::string code;
    std::string message;
    drogon::HttpStatusCode status{drogon::k400BadRequest};
};

/// 仓储层的业务结论映射到 HTTP 错误；Ok 返回空。
std::optional<StandardWriteError> standard_write_error(db::StandardDocumentWriteStatus status);

/**
 * @brief 规范原文库的接口（设计 §7）。
 *
 * 列表要登录，写接口只给管理员。原文内容那条不鉴权：iframe 带不上 Authorization 头，
 * 和桥梁图件、病害照片的内容路由一样；规范原文本身是公开文件，版本 id 是随机 UUID。
 */
void register_standard_document_routes(
    const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config);

}  // namespace bridge_report::http
