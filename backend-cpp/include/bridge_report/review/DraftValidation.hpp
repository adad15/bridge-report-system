#pragma once

#include <string>
#include <vector>

#include <json/value.h>

#include "bridge_report/inventory/ComponentInventoryModels.hpp"
#include "bridge_report/rating_tree/RatingTreeModels.hpp"

namespace bridge_report::review {

/**
 * @brief 校对草稿保存前的问题条目，路径沿用契约校验的 JSONPath 风格。
 */
struct DraftValidationIssue {
    std::string path;
    std::string message;
};

/**
 * @brief PUT review-draft 请求体的校验结果。
 *
 * ok 为 true 时 code/message/issues 均为空。
 * ok 为 false 时 code 取值为：
 *   - import_record_not_editable：导入记录当前状态不是“待校对”
 *   - contract_validation_failed：请求体未通过 BridgeAnnualInspectionData 契约校验（issues 非空）
 *   - import_context_mismatch：请求体 import_context.import_record_system_number 与记录的 system_number 不一致
 */
struct DraftValidationResult {
    bool ok{false};
    std::string code;
    std::string message;
    std::vector<DraftValidationIssue> issues;
};

/**
 * @brief 纯函数：按顺序检查导入记录是否可编辑、请求体是否满足契约、导入上下文是否与记录一致。
 *
 * 检查顺序（返回第一类错误）：
 *   1. record_import_status 不是“待校对” -> import_record_not_editable
 *   2. body 未通过 validate_bridge_annual_inspection_data -> contract_validation_failed
 *   3. body.import_context.import_record_system_number != record_system_number -> import_context_mismatch
 */
[[nodiscard]] DraftValidationResult validate_review_draft(
    const Json::Value& body,
    const std::string& record_system_number,
    const std::string& record_import_status
);

/**
 * @brief 整请求级的台账版本判定：草稿里的绑定病害引用的版本与服务端解析出的版本是否一致。
 *
 * 逐项校验会把每一条版本不符的病害都变成一条 issue；整份草稿的版本统一落后时，
 * 那是"服务端解析出的版本变了"，该整体提示一次并让用户刷新，而不是让用户逐条
 * 重新选择——重新选择写回的仍是同一个版本，解决不了。故先做本判定再做逐项校验。
 *
 * 只统计带 bridge_component_id 的病害；未绑定构件的病害不参与版本判定。
 */
enum class DraftInventoryRevisionConsistency {
    no_bindings,   ///< 没有任何绑定了构件的病害；不涉及版本，也不该锁定年度版本。
    matches,       ///< 全部绑定病害都引用服务端解析出的版本。
    all_stale,     ///< 全部绑定病害一致地引用同一个别的版本 -> 整体一次版本冲突。
    mixed,         ///< 请求内部混用多个版本（含缺版本 id）-> 草稿数据非法，走逐项校验。
    unresolved,    ///< 有绑定病害，但服务端解析不出可用的已确认版本 -> 年度上下文错误。
};

[[nodiscard]] DraftInventoryRevisionConsistency classify_draft_inventory_revision(
    const Json::Value& body,
    const std::optional<std::string>& resolved_revision_id
);

/** 校验病害中的实际构件 ID、规范类别和内部结构部位均来自本检测年度使用的构件台账。 */
[[nodiscard]] DraftValidationResult validate_defect_component_associations(
    const Json::Value& body,
    const std::optional<inventory::InventoryRevision>& resolved_revision
);

/**
 * 服务端规范化草稿中的评定树引用。客户端只能提交一个候选树节点；
 * 年度树版本、H21 指标、自动匹配方式和证据均由服务端覆盖。
 */
[[nodiscard]] DraftValidationResult normalize_defect_rating_tree_associations(
    Json::Value& draft,
    const Json::Value& stored_draft,
    const std::string& rating_tree_version_id,
    const std::string& technical_standard_package_id,
    const rating_tree::EffectiveRatingTree& tree,
    const std::optional<inventory::InventoryRevision>& resolved_revision
);

/** 正式确认前校验每条已处理病害的树节点、构件适用范围和合法标度。 */
[[nodiscard]] DraftValidationResult validate_defect_rating_tree_for_confirmation(
    const Json::Value& draft,
    const std::string& rating_tree_version_id,
    const std::string& technical_standard_package_id,
    const rating_tree::EffectiveRatingTree& tree,
    const std::optional<inventory::InventoryRevision>& resolved_revision
);

/** 已存在病害的 Word 来源和范围拆分来源由服务端锁定，任何重开范围都不得改写。 */
[[nodiscard]] DraftValidationResult validate_imported_defect_evidence(
    const Json::Value& stored_draft,
    const Json::Value& new_draft
);

/**
 * @brief 纯函数：草稿中是否存在带警告的病害候选（defects[i].warnings 为非空数组）。
 *
 * 重开校对 scope=warnings_only 的准入条件：没有警告病害就没有可修正对象。
 */
[[nodiscard]] bool draft_has_warning_defects(const Json::Value& data);

/**
 * @brief 纯函数：warnings_only 重开态的保存范围校验。
 *
 * 以库中已存草稿（重开快照后的当前草稿）为基准：
 *   1. 不允许新增或删除病害候选（candidate_id 集合必须一致）；
 *   2. 基准侧 warnings 为空的病害候选必须逐字段与新草稿完全一致；
 *   3. 带警告病害只能修改白名单业务字段；照片、来源证据和其他顶层数据必须不变；
 * "是否带警告"只看基准侧（存量 JSON），客户端无法通过在请求体里
 * 添改 warnings 数组把锁定病害伪装成可修改。
 *
 * 违规时 ok=false、code="reopen_scope_violation"，issues 指向具体病害候选。
 */
[[nodiscard]] DraftValidationResult validate_warnings_only_scope(
    const Json::Value& stored_draft,
    const Json::Value& new_draft,
    Json::Value* normalized_draft = nullptr
);

/**
 * @brief 比较保存前后的病害 candidate_id，生成可持久化的人工增删审计摘要。
 *
 * 没有增删时返回 JSON null；摘要中的 ID 由服务端对比得出，不信任客户端自报计数。
 */
[[nodiscard]] Json::Value build_defect_change_audit_event(
    const Json::Value& stored_draft,
    const Json::Value& new_draft,
    const std::string& actor_username
);

}  // 命名空间 bridge_report::review
