#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/db/EditLockRepository.hpp"
#include "bridge_report/inventory/SideComponentPair.hpp"

namespace bridge_report::db {

// 下拉里显示一个构件所需的全部信息。entry_id 是 <option> 的 key，
// bridge_component_id 是它的 value，两者不能合并。
// site_name 一并返回：它参与关键词匹配，不显示的话用户看不出为什么命中。
struct BindingComponentSummary {
    std::string entry_id;
    std::string bridge_component_id;
    std::string component_number;
    std::string site_component_type;
    std::string site_name;
};

// 绑定视图的一行 = 报告里出现的一个不同 (部件名称, 归一化编号)。
struct BindingRow {
    std::string component_number;                     // 报告编号原文（代表）
    int defect_count{0};                              // 引用该编号的病害数
    std::string status;                               // bound|ambiguous|unmatched|missing
    std::optional<std::string> bridge_component_id;   // 已绑定的实际构件
    // 歧义候选的内部 id。**不出现在 JSON 里**，但字段必须保留：行状态是否为
    // ambiguous 由它判定，那是业务逻辑，不是显示数据。
    std::vector<std::string> candidate_component_ids;
    // 上面两者对应的展示信息，只填充"启用且有生效映射"的构件。
    // 因此可能出现 candidate_component_ids 有两个、candidate_components 只有一个
    // （构件被删、停用或没有生效映射）——此时行**仍然是 ambiguous**，
    // 展示对象缺失只影响显示，不改变行状态。
    std::optional<BindingComponentSummary> bound_component;
    std::vector<BindingComponentSummary> candidate_components;
    bool split_eligible{false};
    std::optional<int> split_expanded_count;
    // 该行可作为"两侧"整体绑定的左右构件对。与 split_eligible 同一条闸门：
    // 只有未匹配/歧义行才可能有值。判定见 inventory::find_side_component_pair。
    std::optional<inventory::SideComponentPair> side_pair;
};

// 按报告"部件名称"（规范固定用词）分组。
struct BindingGroup {
    std::string part_name;
    int total{0};      // 行数（不同编号数）
    int bound{0};
    int unmatched{0};
    int ambiguous{0};
    int missing{0};
    std::vector<BindingRow> rows;
};

struct BindingRatingTree {
    std::string version_id;
    std::string tree_name;
    std::string package_version;
    std::string h21_package_version;
    std::string maintenance_package_version;
};

struct BindingOverview {
    bool inventory_confirmed{false};
    // 本次概览所依据的台账版本。不变量：有值 当且仅当 inventory_confirmed 为真。
    // 前端拿它作为后续搜索、缓存键与写操作的 expected_inventory_revision_id。
    std::optional<std::string> inventory_revision_id;
    std::optional<BindingRatingTree> rating_tree;
    std::vector<BindingGroup> groups;
};

enum class BindingStatus {
    Ok,
    EditLockInvalid,  // 编辑锁在写事务内已失效（过期或被管理员强制收回）
    NotFound,     // 导入记录不存在
    Conflict,     // 非"待校对"相 / 台账未确认 / 所选构件类别与部件名称不符
    Invalid,      // 入参无效
    TreeNotFound,
    TreeUnavailable,
    MappingIncompatible,
    Failed,       // 数据库异常
};

struct BindingOutcome {
    BindingStatus status{BindingStatus::Ok};
    std::optional<BindingOverview> overview;
    // 批量绑定被拒时回传出错的那个报告编号：整批不写，用户需知道是哪一条挡住的。
    std::string rejected_component_number;
    // 同一个 status 可能对应多种拒绝原因；置了这两项，路由就用它们而不是按状态套用
    // 默认错误码。形状与 ComponentRangeSplitOutcome 一致，三条路径才能共用一套机制。
    std::string error_code;
    std::string error_message;
    // 批量替换取数专用。
    std::string replace_revision_id;
    std::vector<ComponentInventoryRepository::ReplaceEntry> replace_entries;
};

struct BindingTarget {
    std::string part_name;
    std::string component_number;
    std::string bridge_component_id;
};

// 在导入记录"待校对"相内读/改 parsed_result_json.defects[] 的构件绑定。
// 行单位与批量作用域 = (部件名称, 归一化编号)：绑一次挂上所有引用它的病害。
class ImportBindingRepository {
public:
    explicit ImportBindingRepository(drogon::orm::DbClientPtr db_client);

    [[nodiscard]] BindingOutcome overview(const std::string& import_id);

    // 批量替换预览的取数。读操作：只校验 expected_revision_id 仍然有效，不锁定年度。
    // 返回精简条目——预览只用 bridge_component_id / component_number / is_active，
    // 整份台账（含映射、类别、现场名）比它大一个数量级。
    [[nodiscard]] BindingOutcome load_replace_inventory(
        const std::string& import_id, const std::string& expected_revision_id);

    // 以下写操作都带 expected_revision_id：调用方声明"本次操作依据的是这个台账版本"。
    // 事务内解析出的版本与它不符即返回 component_inventory_revision_changed，
    // 不静默改用新版本——否则用户看到的候选来自旧版本，校验却按新版本进行。
    // 年度尚未锁定版本时，校验通过后才把它锁下来。
    //
    // edit_lock 是独占编辑边界的后端那一半：这些接口写的是同一份 parsed_result_json，
    // 不校验锁的话，另一个已登录用户可以在别人持锁时改它，而持锁者的整份草稿保存
    // 又会把这些修改覆盖掉。路由层先拦一道拿到具体的锁失效原因，仓储在事务内复查——
    // 事务外那次检查到真正写入之间，锁可能过期或被管理员强制收回。
    [[nodiscard]] BindingOutcome bind(
        const std::string& import_id, const std::string& part_name,
        const std::string& component_number, const std::string& bridge_component_id,
        const std::string& expected_revision_id,
        const std::optional<EditLockCredentials>& edit_lock = std::nullopt);
    // 批量绑定（供绑定界面的"批量替换"）：单次读改写，任一目标非法则整批不写。
    [[nodiscard]] BindingOutcome bind_batch(
        const std::string& import_id, const std::vector<BindingTarget>& targets,
        const std::string& expected_revision_id,
        const std::optional<EditLockCredentials>& edit_lock = std::nullopt);
    [[nodiscard]] BindingOutcome mark_missing(
        const std::string& import_id, const std::string& part_name,
        const std::string& component_number, const std::string& expected_revision_id,
        const std::optional<EditLockCredentials>& edit_lock = std::nullopt);
    [[nodiscard]] BindingOutcome clear(
        const std::string& import_id, const std::string& part_name,
        const std::string& component_number, const std::string& expected_revision_id,
        const std::optional<EditLockCredentials>& edit_lock = std::nullopt);
    // 为本次导入所属的待校对年度绑定评定树。目标 H21 包不同时从已确认
    // 台账派生新修订，旧规范组合和旧台账不原地覆盖。
    // expected_revision_id 校验的是**源**版本：迁移的起点必须是用户看到的那份台账。
    [[nodiscard]] BindingOutcome bind_rating_tree(
        const std::string& import_id,
        const std::string& rating_tree_version_id,
        const std::string& actor_user_id,
        const std::string& expected_revision_id,
        const std::optional<EditLockCredentials>& edit_lock = std::nullopt);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
