#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/archive/SourceDbReference.hpp"
#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/WordImportRepository.hpp"

namespace bridge_report::http {

struct PythonParseError {
    std::string code;
    std::string message;
};

Json::Value build_python_word_request(
    const db::WordImportContext& context,
    const Json::Value& body,
    const std::filesystem::path& temporary_photo_output_dir
);

Json::Value extract_python_parse_data(const Json::Value& response_body);

std::optional<PythonParseError> extract_python_parse_error(const Json::Value& response_body);

/// 接口同步导入的 Python 请求体。产出的响应与 Word 那条路同形状，
/// 差别只在解析器读的是本机离线库而不是 docx。
Json::Value build_python_source_request(
    const db::WordImportContext& context,
    const Json::Value& body,
    const archive::SourceDbReference& reference,
    const std::filesystem::path& temporary_photo_output_dir
);

/// 该导入记录是否走接口同步（读来源软件离线库）而不是 Word 解析。
[[nodiscard]] bool is_source_db_import(const db::WordImportContext& context);

/**
 * @brief 解析结果落库失败后的清理处置。
 *
 * 两条路差别很大，选错了代价不对称：discard 会删掉导入记录、原始 Word 与已归档的
 * 照片，把一次可重试的失败变成"重新上传一遍"。
 */
enum class PersistFailureDisposition {
    /// 真正的失败：删除导入记录、原始 Word 与归档照片。
    discard,
    /// 可恢复：保留导入记录与原始 Word，只把本次解析转成"解析失败"，让用户重试。
    retain_for_retry,
};

/**
 * @brief 判定一次 persist_parse_result 失败该走哪种清理。
 *
 * 台账版本被并发锁走（component_inventory_revision_changed）是唯一可恢复的一类：
 * Word 已经解析成功、照片也归档了，只是这一次没抢到年度版本，重试即可。其余失败
 * （契约不符、记录已删、状态不对、写库异常）都按原有流程清理。
 *
 * 抽成具名函数是为了能测：它原本是写在路由 lambda 里的一个 if，而这条路径是全项目
 * 唯一会删除用户原始文件的地方，判错方向不可逆。
 */
[[nodiscard]] PersistFailureDisposition disposition_for_persist_failure(
    const std::string& error_code);

void register_word_import_routes(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config
);

}  // namespace bridge_report::http
