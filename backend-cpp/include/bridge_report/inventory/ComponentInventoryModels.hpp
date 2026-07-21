#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

namespace bridge_report::inventory {

enum class NumberingMode { SpanMember, PierLine, Sequential };

std::optional<NumberingMode> parse_numbering_mode(const std::string& value);
std::string to_string(NumberingMode mode);

struct GenerationGroupInput {
    std::string site_component_type;
    std::string site_name;
    std::string standard_component_category_id;
    std::string structure_part;
    NumberingMode numbering_mode{NumberingMode::Sequential};
    int quantity{0};
    std::string number_prefix;
    std::string number_suffix{"#"};
    std::string quantity_key;
};

// 台账向导里用户对一个部件的选择：目录部件键 + 现场名（可改）+ 各数量维。
struct PartSelection {
    std::string part_key;
    std::string site_name;    // 现场名；空则取目录 default_name
    std::vector<int> counts;  // 按目录 count_inputs 顺序
};

struct GenerateInventoryInput {
    std::string standard_package_id;
    std::string template_id;
    std::string bridge_type_id;
    int span_count{0};
    Json::Value input_quantities{Json::objectValue};
    std::vector<GenerationGroupInput> groups;
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

Json::Value inventory_revision_json(const InventoryRevision& revision);
Json::Value inventory_blockers_json(const std::vector<InventoryBlocker>& blockers);

bool parse_generate_inventory_input(
    const Json::Value& body,
    GenerateInventoryInput& output,
    std::string& error_code,
    std::string& error_message);

}  // namespace bridge_report::inventory
