#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

namespace bridge_report::db {

/**
 * @brief 线索绑定事务结果。失败时事务已回滚，error_code 为稳定错误码：
 *   defect_observation_not_found / defect_thread_not_found           -> 404
 *   thread_required_field_missing                                    -> 400
 *   observation_not_current / observation_not_formal /
 *   observation_revision_conflict / thread_component_mismatch /
 *   rebind_confirmation_required /
 *   observation_referenced_by_confirmed_comparison /
 *   observation_already_bound                                        -> 409
 *   db_write_failed / database_commit_failed                         -> 500
 */
struct ThreadBindingOutcome {
    bool success{false};
    std::string error_code;
    std::string error_message;
    Json::Value body;
};

/**
 * @brief 模块 06 唯一的有限写入口：病害线索创建与观测绑定/重绑。
 *
 * 只修改 defect_threads 与 defect_observations.defect_thread_id（组织关系），
 * 绝不修改年度病害事实本身，也不生成发展/减轻/修复/新增结论（属模块 07）。
 * 单一 PostgreSQL 事务内执行模块 06 规格 §9 的九步校验：
 * FOR UPDATE 锁观测 -> 当前有效版本 -> 正式状态 -> updated_at 乐观令牌 ->
 * 同桥同构件 -> 重绑显式确认 -> 已确认对比引用拦截 -> 写绑定 -> 重算首见/末见。
 */
class DefectThreadRepository {
public:
    explicit DefectThreadRepository(drogon::orm::DbClientPtr db_client);

    /// 创建线索并绑定首条观测（观测必须尚未绑定其他线索）。
    /// thread_name 为空时缺省为 "defect_type｜defect_location"。
    ThreadBindingOutcome create_thread(
        const std::string& bridge_component_id,
        const std::string& defect_type,
        const std::string& defect_location,
        const std::string& first_observation_id,
        const std::string& expected_observation_updated_at,
        const std::optional<std::string>& thread_name
    );

    /// 绑定 / 重绑 / 解绑：defect_thread_id 为空时解绑。
    /// 改变既有绑定（换线索或解绑）必须 confirm_rebind=true。
    ThreadBindingOutcome bind_observation(
        const std::string& observation_id,
        const std::optional<std::string>& defect_thread_id,
        const std::string& expected_observation_updated_at,
        bool confirm_rebind
    );

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
