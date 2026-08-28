#pragma once

#include <memory>
#include <string>

#include <drogon/orm/DbClient.h>

namespace bridge_report::resolution {

/**
 * @brief 重开校对的关系态快照（设计 §8.8）。
 *
 * 现有 `reopen_backup_parsed_result_json` 只还原来源草稿，还原不了构件组、目标、实例
 * 和评分树解析。放弃修改却只回滚了一半状态时，症状是"病害文字回到确认时的样子，
 * 绑定却停在重开期间改成的样子"——两半各自看起来都对，合起来是错的。
 *
 * 三个边界都改变**当前状态世代**：重开、放弃修改恢复快照、重新确认成功。事务内必须把
 * 该导入记录所有 `ready` 计划改成 `invalidated`，不能只指望恢复后的对象版本碰巧不同
 * 来挡旧计划——快照恢复可能把版本号一并恢复成计划创建时的值。
 */
struct ReopenSnapshotOutcome {
    bool success{false};
    std::string error_code;
    std::string error_message;
    /// 快照内容校验和；`discard` 没有快照可算时为空。
    std::string checksum;
    /// 本次被作废的 ready 计划数量。
    int invalidated_plan_count{0};
};

using ReopenSnapshotTransaction = std::shared_ptr<drogon::orm::Transaction>;

/// 重开：保存关系态快照，作废未执行计划，记一条 `reopen_snapshot_captured`。
ReopenSnapshotOutcome capture_reopen_snapshot(
    const ReopenSnapshotTransaction& tx,
    const std::string& import_record_id,
    const std::string& actor_user_id);

/// 放弃修改：还原关系态快照，作废未执行计划，记一条 `reopen_snapshot_restored`。
ReopenSnapshotOutcome restore_reopen_snapshot(
    const ReopenSnapshotTransaction& tx,
    const std::string& import_record_id,
    const std::string& actor_user_id);

/// 重新确认成功：删除快照，作废未执行计划，记一条 `reopen_snapshot_discarded`。
ReopenSnapshotOutcome discard_reopen_snapshot(
    const ReopenSnapshotTransaction& tx,
    const std::string& import_record_id,
    const std::string& actor_user_id);

/// 把该导入记录所有 `ready` 计划改成 `invalidated`，返回条数。三个边界共用。
int invalidate_ready_plans(
    const ReopenSnapshotTransaction& tx,
    const std::string& import_record_id,
    const std::string& reason);

}  // namespace bridge_report::resolution
