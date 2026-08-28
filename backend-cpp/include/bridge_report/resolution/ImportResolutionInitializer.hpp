#pragma once

#include <memory>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/inventory/ComponentInventoryModels.hpp"
#include "bridge_report/resolution/ResolutionModels.hpp"

namespace bridge_report::resolution {

/// 导入初始化的上下文。台账版本和年度都允许缺失，缺失时按 §10 的分支处理。
struct ImportResolutionInitializationContext {
    std::string import_record_id;
    std::string bridge_id;
    /// 检测年度 id；为空表示该导入还没挂到年度上，评分树匹配整段跳过。
    std::string inspection_year_id;
    /// 由 resolve_confirmed_revision_ref() 解析出的已确认台账版本；nullopt 表示
    /// 该桥尚无已确认台账，全部组停在 unresolved 且 inventory_revision_id 为空。
    std::optional<inventory::InventoryRevision> revision;
};

struct ImportResolutionInitializationResult {
    bool success{false};
    std::string error_code;
    std::string error_message;
    ResolutionInitializationSummary summary;
};

/**
 * @brief 在保存 parsed_result_json 的同一事务里建立解析状态（设计 §10）。
 *
 * 必须与 JSON 的写入同生共死：出现"JSON 已落库但一条组都没有"的中间态时，界面会
 * 把整份导入显示成没有任何构件行，而重跑导入又会撞上 candidate 唯一约束。
 *
 * 该桥没有已确认台账版本时**不算失败**。那是现网就会出现的正常状态，今天它只让绑定
 * 面板挂出提示，病害与照片校对照常进行；把它当作初始化失败等于把一个局部限制升级成
 * 整条导入不可校对。
 */
[[nodiscard]] ImportResolutionInitializationResult initialize_import_resolution(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const ImportResolutionInitializationContext& context,
    const Json::Value& parsed_result);

}  // namespace bridge_report::resolution
