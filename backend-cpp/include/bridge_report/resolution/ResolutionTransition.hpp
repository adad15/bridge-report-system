#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/inventory/ComponentInventoryModels.hpp"
#include "bridge_report/rating_tree/RatingTreeModels.hpp"
#include "bridge_report/resolution/ResolutionModels.hpp"

// 构件解析状态转换（设计 §9.1）。
//
// 单条命令、批量替换、区间展开和台账版本重指最终都落到这里：目标集合怎么来的是各自
// 的事，"目标变了之后实例、评分树、照片归属该怎么跟着动"只有这一份实现。写两份的话，
// 症状是"单条绑定保住了实例覆盖，批量绑定却把它清了"，而且只在带覆盖的实例上出现。
namespace bridge_report::resolution {

/// 一次转换里选定的目标。顺序即 target_order。
struct ResolutionTargetSelection {
    std::string bridge_component_id;
    std::string target_role{"primary"};
};

/// 评分树上下文。年度没绑评定树时整段跳过，实例留着没有评分树解析行是合法状态。
struct RatingContext {
    std::string rating_tree_version_id;
    std::string technical_standard_package_id;
    rating_tree::EffectiveRatingTree tree;
};

struct ResolutionTransitionInput {
    std::string import_record_id;
    /// 目标状态：bound | unresolved | missing。
    std::string status;
    std::optional<std::string> match_method;
    std::string resolution_mode{"single"};
    std::optional<std::string> inventory_revision_id;
    std::vector<ResolutionTargetSelection> targets;
    std::optional<std::string> actor_user_id;
    /// 审计事件的操作类型，例如 bind / rebind / clear / mark_missing / bulk_replace。
    std::string operation_type;
    std::optional<std::string> plan_id;
};

struct ResolutionTransitionResult {
    bool success{false};
    std::string error_code;
    std::string error_message;
    int group_version{0};
    int instances_created{0};
    int instances_kept{0};
    int instances_deleted{0};
    int rating_rewritten{0};
    int rating_preserved_manual{0};
};

/**
 * @brief 执行一次构件解析状态转换。
 *
 * 必须在调用方的事务里跑：组版本、目标、实例、评分树解析和审计事件要么一起落地，
 * 要么一起不落地。中间态会让界面显示出"已绑定但没有实例"的组。
 *
 * `expected_version` 用于乐观并发；版本对不上时返回 resolution_version_conflict，
 * 不写任何一行。
 */
[[nodiscard]] ResolutionTransitionResult apply_component_resolution_transition(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const std::string& group_id,
    int expected_version,
    const ResolutionTransitionInput& input,
    const Json::Value& parsed_result,
    const std::optional<inventory::InventoryRevision>& revision,
    const std::optional<RatingContext>& rating);

/// 装配年度绑定的评定树上下文；年度未绑或树加载不出来时返回 nullopt。
[[nodiscard]] std::optional<RatingContext> load_rating_context(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const std::string& inspection_year_id);

/**
 * @brief 重算一条实例的评分树解析。
 *
 * 人工选择的节点只在**适用性**变化时失效；纯文字输入变化保留人工节点，仅标记待复核
 * （§8.5）。自动结果两个哈希任一不一致都失效并重新匹配。
 */
void refresh_rating_resolution(
    const std::shared_ptr<drogon::orm::Transaction>& tx,
    const ResolvedDefectInstance& instance,
    const Json::Value& source_defect,
    const std::string& bridge_component_id,
    const inventory::InventoryRevision& revision,
    const RatingContext& rating,
    int component_resolution_version,
    int& rewritten,
    int& preserved_manual);

}  // namespace bridge_report::resolution
