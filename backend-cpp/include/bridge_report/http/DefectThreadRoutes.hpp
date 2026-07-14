#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

namespace bridge_report::http {

/**
 * @brief 模块 06 有限写接口：
 *   POST /api/defect-threads                                    创建线索并绑定首条观测
 *   PUT  /api/defect-observations/{observation_id}/defect-thread 绑定 / 重绑 / 解绑
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

void register_defect_thread_routes(const drogon::orm::DbClientPtr& db_client);

}  // namespace bridge_report::http
