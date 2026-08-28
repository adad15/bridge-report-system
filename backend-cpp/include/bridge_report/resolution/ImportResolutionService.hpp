#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/db/EditLockRepository.hpp"
#include "bridge_report/resolution/ResolutionPlan.hpp"
#include "bridge_report/resolution/ResolutionTransition.hpp"
#include "bridge_report/resolution/ResolutionWorkspaceModels.hpp"

namespace bridge_report::resolution {

enum class ResolutionStatus {
    Ok,
    NotFound,          // 导入记录不存在
    Conflict,          // 导入记录不在"待校对"相
    VersionConflict,   // 组 / 实例 / 评分树解析版本过期
    EditLockInvalid,
    Invalid,           // 入参不合法
    Failed,            // 数据库异常
};

/// 写操作只回受影响对象和最新统计（§13.2），不让前端整页重载。
struct ResolutionCommandResult {
    std::vector<WorkspaceComponentGroup> affected_groups;
    WorkspaceProgress progress;
};

/// 手工新增的响应：新来源病害要先并进本地草稿，再接受新的 draft_version（§16.1）。
struct ManualDefectResult {
    Json::Value source_defect{Json::objectValue};
    int draft_version{1};
    ResolutionCommandResult command_result;
};

struct ResolutionOutcome {
    ResolutionStatus status{ResolutionStatus::Ok};
    std::string error_code;
    std::string error_message;
    std::optional<ResolutionWorkspace> workspace;
    std::optional<ResolutionCommandResult> command_result;
    std::optional<ManualDefectResult> manual_defect;
    std::optional<ResolutionPlanPreview> plan;
    /// 应用计划的结果。重放已成功计划时原样返回首次的那一份。
    std::optional<Json::Value> apply_result;
};

/// 所有写命令共有的前置：编辑锁、导入记录、乐观并发版本。
struct ResolutionCommandContext {
    std::string import_record_id;
    std::optional<db::EditLockCredentials> edit_lock;
    std::optional<std::string> actor_user_id;
    /// 客户端看到的台账版本；与当前解析出的版本不一致时拒绝（沿用既有参数名）。
    std::string expected_inventory_revision_id;
};

/// 单个构件解析命令（§13.2）。
struct ComponentResolutionRequest {
    ResolutionCommandContext context;
    std::string group_id;
    int expected_version{0};
    /// bind | mark_missing | clear
    std::string action;
    /// bind 时选中的目标；顺序即 target_order。
    std::vector<ResolutionTargetSelection> targets;
};

struct RatingResolutionRequest {
    ResolutionCommandContext context;
    std::string instance_id;
    int expected_version{0};
    /// 人工选定的节点；为空表示清除人工选择、退回未解析。
    std::string rating_tree_node_id;
    std::string expected_rating_tree_version_id;
};

/// 一条待写实例及其评分树解析版本。没有解析行时版本是 0。
struct RatingInstanceVersion {
    std::string instance_id;
    int expected_version{0};
};

/**
 * @brief 按**来源病害**整体写评分树解析。
 *
 * 校对页一条来源病害显示一行（§22.6），用户选一次节点，落到这条病害的全部活动实例。
 * 逐实例接口逐条发请求时，取草稿、装评定树、鉴权、查编辑锁、开事务、提交 fsync 全部
 * 乘以实例数——区间展开的病害是 25 次，实测 2 秒。这条命令把那些一次性开销摊成一份，
 * 并且全部实例同一事务：要么整条病害写成，要么一条都不写。校对页那一行显示的是整条
 * 病害的结论，写进去一半比不写更难查。
 */
struct SourceRatingResolutionRequest {
    ResolutionCommandContext context;
    /// 人工选定的节点；为空表示清除、退回未解析。
    std::string rating_tree_node_id;
    std::string expected_rating_tree_version_id;
    /// 这条来源病害的全部活动实例，各带自己的解析版本。
    std::vector<RatingInstanceVersion> instances;
};

struct FactOverrideRequest {
    ResolutionCommandContext context;
    std::string instance_id;
    int expected_version{0};
    /// 要写入的覆盖值；键出现即生效。
    Json::Value overrides{Json::objectValue};
    /// 要清除的覆盖字段。清除是删键，不是写 null。
    std::vector<std::string> cleared_fields;
};

struct InstanceStatusRequest {
    ResolutionCommandContext context;
    std::string instance_id;
    int expected_version{0};
    std::string instance_status;  // active | ignored
};

/// 手工新增病害（§4.6）：唯一同时写来源事实与解析状态的命令。
struct ManualDefectRequest {
    ResolutionCommandContext context;
    /// 来源草稿并发版本，走 If-Match 头（§8.0）。
    int expected_draft_version{0};
    /// 用户选定的台账构件。组的来源名称与编号取自该条目本身。
    std::string bridge_component_id;
    /// 用户选定的评定树节点。手工新增必须给出，它是这条命令存在的理由之一。
    std::string rating_tree_node_id;
    /// 来源病害事实：defect_type / defect_location / defect_description /
    /// defect_scale / quantity_text / measurement_text / measurements / remark。
    Json::Value defect_facts{Json::objectValue};
};

/**
 * @brief 构件解析与评分树解析业务规则的唯一编排位置（设计 §12）。
 *
 * 仓储只负责数据库读写；状态该不该变、目标合不合法、哈希是否失效、允许哪些动作，
 * 全部在这里判定。路由层只做参数解析与 DTO 序列化，不复制任何一条规则。
 */
class ImportResolutionService {
public:
    explicit ImportResolutionService(drogon::orm::DbClientPtr db_client);

    /**
     * @brief 生成解析工作区读模型（§13.1）。
     *
     * 响应自带部件层级聚合、歧义标签、进度统计和允许动作，前端不再从原始病害数组
     * 自行聚合。台账未确认时照常返回工作区，只是绑定类动作全部不可用——那是现网就
     * 会出现的正常状态，不是错误。
     */
    [[nodiscard]] ResolutionOutcome load_workspace(const std::string& import_record_id) const;

    /**
     * @brief 单构件、多目标、标记缺失与清除（§9.1、§13.2）。
     *
     * 候选来自后端不等于可以信任客户端目标：这里重新校验目标属于当前桥梁、属于组所
     * 钉的台账版本、且部件类别允许。
     */
    [[nodiscard]] ResolutionOutcome apply_component_resolution(
        const ComponentResolutionRequest& request) const;

    /// 人工选择评分树节点，或清除人工选择退回未解析。
    [[nodiscard]] ResolutionOutcome apply_rating_resolution(
        const RatingResolutionRequest& request) const;

    /// 按来源病害整体写评分树解析：一次事务写完它的全部活动实例。
    [[nodiscard]] ResolutionOutcome apply_source_rating_resolution(
        const SourceRatingResolutionRequest& request) const;

    /// 实例级事实覆盖；涉及匹配输入的变化后按新的有效值重算评分树解析。
    [[nodiscard]] ResolutionOutcome apply_fact_overrides(
        const FactOverrideRequest& request) const;

    /// 实例状态切换（§9.3），事务内重算照片归属。
    [[nodiscard]] ResolutionOutcome apply_instance_status(
        const InstanceStatusRequest& request) const;

    /**
     * @brief 手工新增病害（§4.6）。
     *
     * 在一个事务里追加来源病害、建组或复用组、写目标、生成实例、写人工评分树解析。
     * 它是唯一打破"来源事实归 JSON、解析状态归关系表"这条分工的接口，因此**不得扩张**：
     * 任何"顺便也支持改点别的"的需求都退回各自的接口。
     */
    [[nodiscard]] ResolutionOutcome add_manual_defect(
        const ManualDefectRequest& request) const;

    /**
     * @brief 生成批量替换预览计划（§13.3）。
     *
     * 参与范围只含未解析组：已绑定与已标记缺失的组不参与、不受影响，守住"已经核对过
     * 的结果不会被批量操作意外推翻"。
     */
    [[nodiscard]] ResolutionOutcome build_bulk_replace_plan(
        const ResolutionCommandContext& context, const BulkReplaceIntent& intent) const;

    /// 生成区间展开预览计划。
    [[nodiscard]] ResolutionOutcome build_range_expand_plan(
        const ResolutionCommandContext& context, const RangeExpandIntent& intent) const;

    /// 生成台账版本重指预览计划（§9.4）。
    [[nodiscard]] ResolutionOutcome build_inventory_repoint_plan(
        const ResolutionCommandContext& context,
        const InventoryRepointIntent& intent) const;

    /**
     * @brief 应用预览计划（§14）。
     *
     * 同一事务内按顺序处理：已应用先重放、再校验未过期、锁 token 一致、版本前提未变。
     * 任一检查失败则整批不写入——不尝试"尽可能执行"。
     */
    [[nodiscard]] ResolutionOutcome apply_resolution_plan(
        const ResolutionCommandContext& context, const std::string& plan_token) const;

private:
    /// 命令成功后组装"受影响对象 + 最新统计"。与工作区共用同一份组视图构建代码，
    /// 免得命令返回的组和刷新后看到的组对不上。
    /// 上面两个评分树命令的共同实现：一次事务写完给定的全部实例。
    [[nodiscard]] ResolutionOutcome write_rating_resolutions(
        const ResolutionCommandContext& context,
        const std::vector<RatingInstanceVersion>& instances,
        const std::string& rating_tree_node_id,
        const std::string& expected_rating_tree_version_id) const;

    [[nodiscard]] ResolutionOutcome build_command_result(
        const std::string& import_record_id,
        const std::vector<std::string>& group_ids) const;

    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::resolution
