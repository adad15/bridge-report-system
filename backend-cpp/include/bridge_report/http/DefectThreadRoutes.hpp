#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

namespace bridge_report::http {

/**
 * @brief 模块 06 有限写接口：
 *   POST /api/defect-threads                                    创建线索并绑定首条观测
 *   PUT  /api/defect-observations/{observation_id}/defect-thread 绑定 / 重绑 / 解绑
 *   POST /api/bridges/{bridge_id}/thread-triage/apply            批量建线索 / 批量绑定
 *
 * 只改变观测与线索的组织关系，不修改年度病害事实；系统只给候选建议，
 * 绑定一律由人工发起并携带 updated_at 乐观令牌。
 */

struct CreateThreadRequest {
    std::string bridge_component_id;
    std::string defect_type;
    std::string defect_location;
    std::string first_observation_id;
    std::string expected_observation_updated_at;
    std::optional<std::string> thread_name;
};

struct BindObservationRequest {
    std::optional<std::string> defect_thread_id;  // 空 = 解绑
    std::string expected_observation_updated_at;
    bool confirm_rebind{false};
};

// 请求体解析纯函数：返回稳定错误码（400 级），成功时返回空。
[[nodiscard]] std::optional<std::string> parse_create_thread_request(
    const Json::Value& body, CreateThreadRequest& out);
[[nodiscard]] std::optional<std::string> parse_bind_observation_request(
    const Json::Value& body, BindObservationRequest& out);

/**
 * @brief 批量应用请求。
 *
 * 请求只表达"用户选了什么"：服务端会从锁定后的数据库行重新计算组的构件、类型、位置与
 * 精确匹配，客户端给的这些值一律不作为业务事实（见 `ThreadResolutionRepository`）。
 */
struct TriageApplyRequestBody {
    std::string batch_id;
    std::string batch_fingerprint;
    std::string idempotency_key;
    /// "create" / "bind"
    std::string action;
    struct Group {
        std::string group_id;
        std::string target_thread_id;
        struct Observation {
            std::string id;
            std::string updated_at;
        };
        std::vector<Observation> observations;
    };
    std::vector<Group> groups;
};

/// 服务端上限：超出直接拒绝，不由前端拆分——拆开就不再是一个事务，"全成或全败"随之作废。
inline constexpr int kTriageApplyMaxGroups = 500;

[[nodiscard]] std::optional<std::string> parse_triage_apply_request(
    const Json::Value& body, TriageApplyRequestBody& out);

void register_defect_thread_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
