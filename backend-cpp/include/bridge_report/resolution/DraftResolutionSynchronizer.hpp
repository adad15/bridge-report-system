#pragma once

#include <memory>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/inventory/ComponentInventoryModels.hpp"

// 普通草稿保存与解析关系的同步（设计 §11.1）。
//
// 保存与同步必须在同一个 PostgreSQL 事务里完成：出现"JSON 已保存而成员/评分树状态未
// 同步"的中间态时，新增的病害在绑定工作区里根本不存在，删掉的病害却还占着组。
namespace bridge_report::resolution {

struct DraftSyncResult {
    bool ok{false};
    std::string error_code;
    std::string error_message;

    int members_added{0};
    int members_removed{0};
    int groups_removed{0};
    int ratings_recomputed{0};
};

/**
 * @brief 比较旧草稿与新草稿，把差异同步到解析关系表。
 *
 * 处理的差异：
 *   - 新增 `candidate_id`：创建或复用来源构件组并增加成员；组已绑定时生成实例并
 *     执行评分树匹配。这条分支只兜住"没有明确目标的新增"——界面上的手工新增走
 *     专用命令（§4.6），不经普通保存。
 *   - 删除 `candidate_id`：删除成员，级联删除实例和评分树解析；空构件组随之删除。
 *   - 修改病害类型、位置、描述或来源分组/指标身份：按**有效值**重算哈希，自动结果
 *     重新匹配，人工结果保留节点（§8.5）。
 *   - 仅修改尺寸、数量、标度、备注或照片关系：不重新选择评分树节点。
 *   - 修改来源构件名称或编号：拒绝，返回 `source_component_identity_immutable`。
 */
[[nodiscard]] DraftSyncResult synchronize_draft_resolution(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const std::string& import_record_id,
    const std::string& bridge_id,
    const std::string& inspection_year_id,
    const Json::Value& stored_draft,
    const Json::Value& new_draft,
    const std::optional<inventory::InventoryRevision>& revision);

}  // namespace bridge_report::resolution
