#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

namespace bridge_report::inventory {

// 台账向导里用户对一个部件的选择：目录部件键 + 现场名（可改）+ 各数量维。
struct PartSelection {
    std::string part_key;
    std::string site_name;    // 现场名；空则取目录 default_name
    std::vector<int> counts;  // 按目录 count_inputs 顺序
    // 用户在向导里去掉的位置（仅 instance_selectable 部件用），元素须是该部件展开出的编号。
    std::vector<std::string> excluded_numbers;
};

struct GenerateInventoryInput {
    std::string standard_package_id;
    std::string template_id;  // 溯源；目录路径留空，仓库写哨兵
    std::string bridge_type_id;
    int span_count{0};
    Json::Value input_quantities{Json::objectValue};  // 溯源
    std::vector<PartSelection> part_selections;
};

struct GeneratedInventoryEntry {
    std::string generation_key;
    std::string component_number;
    std::string site_name;
    std::string site_component_type;
    std::optional<std::string> span_or_location;
    std::string standard_component_category_id;
    std::string structure_part;
    int sort_order{0};
};

struct InventoryMapping {
    std::string id;
    std::string standard_package_id;
    std::string standard_bridge_type_id;
    std::string standard_component_category_id;
    std::string structure_part;
    std::string mapping_source;
    std::string confirmation_status;
    bool is_active{true};
};

struct InventoryEntry {
    std::string id;
    std::string bridge_component_id;
    std::string component_number;
    std::string site_name;
    std::string site_component_type;
    std::optional<std::string> span_or_location;
    bool is_active{true};
    std::optional<std::string> deactivated_at;
    std::optional<std::string> deactivation_reason;
    int sort_order{0};
    std::optional<std::string> remarks;
    bool is_referenced{false};
    std::vector<InventoryMapping> mappings;
};

struct InventoryRevision {
    std::string id;
    std::string bridge_id;
    int revision_number{0};
    std::string status;
    std::optional<std::string> baseline_revision_id;
    std::optional<std::string> confirmed_at;
    std::vector<InventoryEntry> entries;
};

struct InventoryBlocker {
    std::string code;
    std::string entity_type;
    std::string entity_id;
    std::string field_path;
    std::string message;
};

// 单条构件的序列化。分组分页、编号搜索、写响应里的受影响构件共用这一份；
// inventory_revision_json() 也改成调它，避免同一形状写两遍。
Json::Value inventory_entry_json(const InventoryEntry& entry);
// 同上，另带组内序号（从 0 起），供前端算页码与定位。
Json::Value located_entry_json(const InventoryEntry& entry, std::int64_t position);
Json::Value inventory_revision_json(const InventoryRevision& revision);
Json::Value inventory_blockers_json(const std::vector<InventoryBlocker>& blockers);

bool parse_generate_inventory_input(
    const Json::Value& body,
    GenerateInventoryInput& output,
    std::string& error_code,
    std::string& error_message);

}  // namespace bridge_report::inventory
