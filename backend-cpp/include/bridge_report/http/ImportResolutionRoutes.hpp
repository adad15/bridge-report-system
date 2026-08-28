#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "bridge_report/resolution/ImportResolutionService.hpp"
#include "bridge_report/resolution/ResolutionWorkspaceModels.hpp"

namespace bridge_report::http {

// 工作区读模型序列化（供前端与序列化契约测试）。
[[nodiscard]] Json::Value resolution_workspace_json(
    const resolution::ResolutionWorkspace& workspace);

// 写命令响应序列化：只回受影响对象和最新统计（§13.2）。
[[nodiscard]] Json::Value resolution_command_result_json(
    const resolution::ResolutionCommandResult& result);

// 手工新增响应序列化：新来源病害 + 新草稿版本 + 受影响对象（§4.6、§16.1）。
[[nodiscard]] Json::Value resolution_manual_defect_json(
    const resolution::ManualDefectResult& result);

// 服务结果 → 错误码 + 提示 + HTTP 状态。
//
// 与绑定路由同样的理由抽出来：把这段决策埋在 lambda 里，"哪个码配哪个状态"就没人
// 验得了，而那正是最容易出错的地方。
struct ResolutionErrorResponse {
    std::string error_code;
    std::string error_message;
    int http_status{200};
};

[[nodiscard]] ResolutionErrorResponse resolution_error_response(
    const resolution::ResolutionOutcome& outcome);

void register_import_resolution_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
