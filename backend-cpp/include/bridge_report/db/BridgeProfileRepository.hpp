#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "bridge_report/report/BridgeProfileModels.hpp"

namespace bridge_report::db {

/**
 * @brief 桥梁档案里「这座桥本身」那部分的读写。
 *
 * 报告 §1.1 的三段叙述、附录2 卡片的十几格，取的都是这一份数据。同一个事实只存一
 * 处，不在报告侧另抄一份。
 *
 * 写是**整体覆盖**，不做增量合并：编辑界面一次提交一整张档案表，把某一项清空是
 * 正当操作（原来录错了），增量合并会让"清空"变得无法表达。
 */
class BridgeProfileRepository {
public:
    explicit BridgeProfileRepository(drogon::orm::DbClientPtr client);

    /// 桥梁不存在时返回空。
    std::optional<report::BridgeProfile> find(const std::string& bridge_id) const;

    report::BridgeProfileWriteStatus save(
        const std::string& bridge_id, const report::BridgeProfileInput& input) const;

private:
    drogon::orm::DbClientPtr client_;
};

}  // namespace bridge_report::db
