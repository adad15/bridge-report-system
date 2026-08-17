#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/inventory/ComponentInventoryGenerator.hpp"

namespace bridge_report::db {

enum class ComponentInventoryStatus {
    Ok,
    NotFound,
    Invalid,
    Conflict,
    Referenced,
    Blocked,
    // 客户端拿着的已确认版本已被别的草稿取代：桥上存在基于另一版本的草稿，
    // 再派生一条会让一桥出现两条草稿分支。与 Conflict 分开，是因为前端要据此
    // 重新拉取台账并采纳新的修订版 id，而不是只弹一句"已变化"。
    Superseded,
    Failed,
};

struct InventoryEntryUpdate {
    std::string component_number;
    std::string site_name;
    std::string site_component_type;
    std::optional<std::string> span_or_location;
    std::optional<std::string> remarks;
};

struct InventoryNewEntry : InventoryEntryUpdate {
    int sort_order{0};
};

struct InventoryMappingUpdate {
    std::string standard_package_id;
    std::string standard_bridge_type_id;
    std::string standard_component_category_id;
    std::string structure_part;
    std::string mapping_source{"人工选择"};
};

struct ComponentInventoryOutcome {
    ComponentInventoryStatus status{ComponentInventoryStatus::Failed};
    // 写入后的分组汇总，在提交前的同一个事务里算出，提交确认后才返回。
    std::optional<Json::Value> summary;
    // 被改动的那一条构件（新增 / 修改 / 停用 / 设映射时有；删除、批量确认、
    // 确认台账、生成台账没有）。同样在事务内取，事务外按 id 重读会破坏同快照。
    std::optional<inventory::LocatedInventoryEntry> entry;
    std::optional<std::string> entry_id;
    std::vector<inventory::InventoryBlocker> blockers;
};

class ComponentInventoryRepository {
public:
    explicit ComponentInventoryRepository(drogon::orm::DbClientPtr db_client);

    std::optional<inventory::InventoryRevision> get_revision(const std::string& revision_id) const;
    std::optional<inventory::InventoryRevision> get_latest_revision(const std::string& bridge_id) const;

    // 只解析出最新修订版的 id，不装配构件与映射。
    // 汇总端点必须走这条，不能用 get_latest_revision()——后者内部调 get_revision()，
    // 会把全部构件连同映射装配一遍；复用它的话响应体虽小，后端仍完整跑一次全量装配，
    // 优化只做了一半，而且从响应上完全看不出来。
    std::optional<std::string> find_latest_revision_id(const std::string& bridge_id) const;

    // 分组汇总。整份结果由一条语句产出，靠单语句快照保证 revision / groups / blockers
    // 三段来自同一时点——拆成多条查询时，默认的 READ COMMITTED 会让每条 SELECT 各取
    // 一个新快照，出现"blocker 报未映射构件但 unmapped_count 全为 0"这类自相矛盾。
    //
    // 返回的是最终响应形状的 JSON 而不是结构体：既然快照要求逼出了单语句，
    // 异构的三段就只能在 SQL 里用 json_build_object 拼；再解析回结构体又序列化回去
    // 是纯粹的往返开销。代价是响应形状落在了 SQL 里，改形状要改 SQL。
    std::optional<Json::Value> load_summary(const std::string& revision_id) const;

    // 分组分页与编号搜索共用的返回形状。page / size 由路由按请求回显，不在这里存。
    struct EntryLookup {
        std::int64_t total{0};
        std::vector<inventory::LocatedInventoryEntry> entries;
    };

    // 某一类别的构件，按 (sort_order, id) 分页。含停用构件——它们在分组弹窗里
    // 仍然可见并占位，过滤掉会让页码与"定位"对不上。
    EntryLookup load_group_entries(
        const std::string& revision_id,
        const std::string& site_component_type,
        std::int64_t offset,
        std::int64_t limit) const;

    // 按编号子串检索，语义与前端原来的 String.includes 一致（搜 3-5 也会命中 13-5#梁）。
    // total 是未截断的命中数，界面上"匹配 N 个构件，显示前 M 个"依赖它。
    EntryLookup search_entries(
        const std::string& revision_id,
        const std::string& number_fragment,
        std::int64_t limit) const;

private:
    // executor 传连接就是普通只读路径，传当前事务就是写路径——同一段 SQL 复用，
    // 不产生嵌套事务，也不会出现两套规则。
    static std::optional<Json::Value> load_summary_with(
        const drogon::orm::DbClientPtr& executor,
        const std::string& revision_id);

    // 分组分页与编号搜索的共同实现，差别只在 scope_predicate（$2 是它的取值）。
    static EntryLookup load_entry_page(
        const drogon::orm::DbClientPtr& executor,
        const std::string& revision_id,
        const std::string& scope_predicate,
        const std::string& scope_value,
        std::int64_t offset,
        std::int64_t limit);

    // 单条构件，连同它的组内序号。写响应用它，取自写入所在的那个事务。
    static std::optional<inventory::LocatedInventoryEntry> load_entry_with(
        const drogon::orm::DbClientPtr& executor,
        const std::string& revision_id,
        const std::string& entry_id);

public:

    ComponentInventoryOutcome generate_draft(
        const std::string& bridge_id,
        const std::string& user_id,
        const inventory::GenerateInventoryInput& input,
        const std::vector<inventory::GeneratedInventoryEntry>& generated);
    ComponentInventoryOutcome update_entry(
        const std::string& revision_id,
        const std::string& entry_id,
        const std::string& user_id,
        const InventoryEntryUpdate& update);
    ComponentInventoryOutcome add_entry(
        const std::string& revision_id,
        const std::string& user_id,
        const InventoryNewEntry& entry);
    ComponentInventoryOutcome delete_entry(
        const std::string& revision_id,
        const std::string& entry_id,
        const std::string& user_id);
    ComponentInventoryOutcome deactivate_entry(
        const std::string& revision_id,
        const std::string& entry_id,
        const std::string& user_id,
        const std::string& reason);
    ComponentInventoryOutcome set_mapping(
        const std::string& revision_id,
        const std::string& entry_id,
        const std::string& user_id,
        const InventoryMappingUpdate& mapping);
    ComponentInventoryOutcome confirm_pending_mappings(
        const std::string& revision_id,
        const std::string& user_id,
        const std::string& site_component_type);
    ComponentInventoryOutcome confirm_revision(
        const std::string& revision_id,
        const std::string& user_id,
        const std::string& note);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
