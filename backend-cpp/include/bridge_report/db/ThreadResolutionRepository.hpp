#pragma once

#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/review/ThreadTriageGrouping.hpp"

namespace bridge_report::db {

/**
 * @brief 批量落库：为整理批次建线索、绑观测。
 *
 * 三条铁律，改动前先读：
 *
 * **一、不信任客户端提交的业务事实。** 请求里的 `group_id`、`action`、`target_thread_id`
 * 和观测清单只表达"用户选了什么"。组属于哪个构件、是什么病害、在什么位置，一律从锁定后的
 * 数据库行重算。否则前端一个缺陷就能把跨桥、跨构件的观测塞进同一组，绕过全部归组语义。
 *
 * **二、整批一个事务，全成或全败。** 批次的语义是"这 N 个构件适用同一个判断"，部分成功会
 * 留下一个谁也说不清的中间态。校验分三阶段：批量锁定 → 只读校验并**收集全部错误** →
 * 批量写入。绝不"处理一组写一组"——那样只能报出第一个错误，兑现不了失败明细。
 *
 * **三、模块 07 引用检查必须显式调用。** `validate_observation_state` 只做三项
 * （年度当前有效、观测是正式事实、令牌匹配）；引用检查是独立的
 * `referenced_by_confirmed_comparison`，而且现有代码只在重绑路径上调它，首次绑定根本不查。
 * 批量 create 走的正是首次绑定那条路。
 */

struct TriageApplyObservation {
    std::string id;
    std::string updated_at;
};

struct TriageApplyGroup {
    std::string group_id;
    /// 空表示 create；bind 时必填。服务端仍会自行重算并核对。
    std::string target_thread_id;
    std::vector<TriageApplyObservation> observations;
};

struct TriageApplyRequest {
    std::string bridge_id;
    std::string batch_id;
    std::string batch_fingerprint;
    std::string idempotency_key;
    review::TriageAction action{review::TriageAction::Create};
    std::vector<TriageApplyGroup> groups;
};

struct TriageApplyIssue {
    std::string code;
    std::string message;
    std::string group_id;
    std::string bridge_component_id;
    std::string observation_id;
};

struct TriageGroupResult {
    std::string group_id;
    std::string bridge_component_id;
    std::string thread_id;
    std::string thread_system_number;
    /// created / bound / already_completed
    std::string outcome;
};

enum class TriageApplyStatus {
    Applied,
    AlreadyCompleted,
    Rejected,
    BatchChanged,
};

struct TriageApplyOutcome {
    TriageApplyStatus status{TriageApplyStatus::Rejected};
    std::vector<TriageGroupResult> results;
    std::vector<TriageApplyIssue> issues;
    int threads_created{0};
    int observations_bound{0};
};

class ThreadResolutionRepository {
public:
    explicit ThreadResolutionRepository(drogon::orm::DbClientPtr db_client);

    [[nodiscard]] TriageApplyOutcome apply(const TriageApplyRequest& request) const;

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
