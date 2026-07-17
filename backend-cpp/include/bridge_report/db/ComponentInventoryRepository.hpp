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
    ComponentInventoryOutcome confirm_revision(
        const std::string& revision_id,
        const std::string& user_id,
        const std::string& note);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
