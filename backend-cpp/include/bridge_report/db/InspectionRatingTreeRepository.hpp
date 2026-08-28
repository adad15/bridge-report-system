#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "bridge_report/db/EditLockRepository.hpp"

// 年度评定树绑定。
//
// 5.0 之前这里还管构件绑定、标记缺失、取消绑定、批量替换与区间展开，那些写操作把
// 解析结论存进 import_records.parsed_result_json。解析状态搬进关系表后它们全部下线：
// 它们写的 bridge_component_id、rating_tree_node_id 等字段已从契约删除，调一次就把
// 草稿写成非法契约（设计 §21 明确禁止新关系表与旧 JSON 字段双写）。
//
// 只剩下的这一个方法绑的是「本年度用哪一版评定树」，与「某条病害挂到哪件构件」无关，
// 不属于构件解析那套状态，因此留在原地。
namespace bridge_report::db {

enum class RatingTreeBindingStatus {
    Ok,
    EditLockInvalid,  // 编辑锁在写事务内已失效（过期或被管理员强制收回）
    NotFound,     // 导入记录不存在
    Conflict,     // 非"待校对"相 / 台账未确认 / 年度已有成功正式评定
    Invalid,      // 入参无效
    TreeNotFound,
    TreeUnavailable,
    MappingIncompatible,
    Failed,       // 数据库异常
};

struct RatingTreeBindingOutcome {
    RatingTreeBindingStatus status{RatingTreeBindingStatus::Ok};
    // 同一个 status 可能对应多种拒绝原因；置了这两项，路由就用它们而不是按状态套用
    // 默认错误码。
    std::string error_code;
    std::string error_message;
};

class InspectionRatingTreeRepository {
public:
    explicit InspectionRatingTreeRepository(drogon::orm::DbClientPtr db_client);

    /**
     * @brief 为年度绑定（或切换）评定树版本。
     *
     * 成功时不回概览：调用方绑完会重取解析工作区，那是当前状态的唯一来源。此前这里
     * 返回一份完整概览，两处各自表达"现在是什么样"，迟早会不一致。
     */
    [[nodiscard]] RatingTreeBindingOutcome bind_rating_tree(
        const std::string& import_id,
        const std::string& rating_tree_version_id,
        const std::string& actor_user_id,
        const std::string& expected_revision_id,
        const std::optional<EditLockCredentials>& edit_lock = std::nullopt);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
