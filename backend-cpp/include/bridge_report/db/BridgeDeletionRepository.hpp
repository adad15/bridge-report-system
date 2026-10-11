#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "bridge_report/deletion/BridgeDeletionModels.hpp"

namespace bridge_report::db {

class BridgeDeletionRepository {
public:
    explicit BridgeDeletionRepository(drogon::orm::DbClientPtr db_client);

    std::optional<deletion::BridgeDeletionPlan> preview(const std::string& bridge_id) const;
    deletion::DeleteBridgeOutcome delete_bridge(
        const std::string& bridge_id,
        const std::string& expected_impact_token,
        const std::string& reason,
        const deletion::DeletionActorSnapshot& actor,
        const std::string& batch_id
    );

private:
    // 一次完整尝试：开事务、检查、删除、等提交确认。出错先回滚再原样抛出，
    // 由 delete_bridge 决定是否因死锁整段重来。
    deletion::DeleteBridgeOutcome delete_bridge_once(
        const std::string& bridge_id,
        const std::string& expected_impact_token,
        const std::string& reason,
        const deletion::DeletionActorSnapshot& actor,
        const std::string& batch_id
    );

    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
