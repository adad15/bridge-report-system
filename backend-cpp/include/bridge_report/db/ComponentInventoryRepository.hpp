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
    std::optional<inventory::InventoryRevision> revision;
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
