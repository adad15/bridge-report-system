#pragma once

#include <filesystem>

#include <drogon/orm/DbClient.h>

namespace bridge_report::http {

/**
 * @brief 注册模块 06 只读构件病害档案查询路由（全部 GET）：
 *
 *   GET /api/bridges/{bridge_id}/components
 *   GET /api/bridge-components/{component_id}/defect-archive
 *   GET /api/bridge-components/{component_id}/defect-archive/revisions
 *   GET /api/bridges/{bridge_id}/unbound-defect-observations
 *   GET /api/defect-observations/{observation_id}/evidence
 *   GET /api/defect-photos/{defect_photo_id}/content
 *   GET /api/bridges/{bridge_id}/thread-triage                     线索整理工作台摘要
 *   GET /api/bridges/{bridge_id}/thread-triage/batches/{batch_id}  批次明细
 *
 * 主页面只读优先：本文件不含任何写路由；线索建议与绑定见 DefectThreadRoutes。
 * 照片经受控接口按归档相对路径解析，绝不向前端暴露服务器绝对路径。
 */
void register_component_archive_routes(
    const drogon::orm::DbClientPtr& db_client,
    const std::filesystem::path& archive_root
);

}  // namespace bridge_report::http
