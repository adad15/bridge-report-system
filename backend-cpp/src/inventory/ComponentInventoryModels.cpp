#include "bridge_report/inventory/ComponentInventoryModels.hpp"

namespace bridge_report::inventory {
namespace {

bool non_empty_string(const Json::Value& value, const char* key, std::string& output) {
    if (!value.isMember(key) || !value[key].isString()) return false;
    output = value[key].asString();
    return !output.empty();
}

Json::Value mapping_json(const InventoryMapping& mapping) {
    Json::Value value;
    value["id"] = mapping.id;
    value["standard_package_id"] = mapping.standard_package_id;
    value["standard_bridge_type_id"] = mapping.standard_bridge_type_id;
    value["standard_component_category_id"] = mapping.standard_component_category_id;
    value["structure_part"] = mapping.structure_part;
    value["mapping_source"] = mapping.mapping_source;
    value["confirmation_status"] = mapping.confirmation_status;
    value["is_active"] = mapping.is_active;
    return value;
}

}  // namespace

// 分组分页、编号搜索和写响应里的单条构件都用它。整份修订版的序列化随 /latest
// 一起去掉了——那是最后一个会把五千多条构件一次性发出去的地方。
Json::Value inventory_entry_json(const InventoryEntry& entry) {
    Json::Value item;
    item["id"] = entry.id;
    item["bridge_component_id"] = entry.bridge_component_id;
    item["component_number"] = entry.component_number;
    item["site_name"] = entry.site_name;
    item["site_component_type"] = entry.site_component_type;
    item["span_or_location"] = entry.span_or_location.has_value()
        ? Json::Value(*entry.span_or_location) : Json::Value(Json::nullValue);
    item["is_active"] = entry.is_active;
    item["deactivated_at"] = entry.deactivated_at.has_value()
        ? Json::Value(*entry.deactivated_at) : Json::Value(Json::nullValue);
    item["deactivation_reason"] = entry.deactivation_reason.has_value()
        ? Json::Value(*entry.deactivation_reason) : Json::Value(Json::nullValue);
    item["sort_order"] = entry.sort_order;
    item["remarks"] = entry.remarks.has_value()
        ? Json::Value(*entry.remarks) : Json::Value(Json::nullValue);
    item["is_referenced"] = entry.is_referenced;
    item["mappings"] = Json::Value(Json::arrayValue);
    for (const auto& mapping : entry.mappings) item["mappings"].append(mapping_json(mapping));
    return item;
}

// 分组分页与搜索结果里的构件多带一个组内序号，供前端算页码、定位到具体一行。
Json::Value located_entry_json(const InventoryEntry& entry, std::int64_t position) {
    Json::Value item = inventory_entry_json(entry);
    item["position"] = static_cast<Json::Int64>(position);
    return item;
}

Json::Value inventory_blockers_json(const std::vector<InventoryBlocker>& blockers) {
    Json::Value value(Json::arrayValue);
    for (const auto& blocker : blockers) {
        Json::Value item;
        item["code"] = blocker.code;
        item["entity_type"] = blocker.entity_type;
        item["entity_id"] = blocker.entity_id;
        item["field_path"] = blocker.field_path;
        item["message"] = blocker.message;
        value.append(std::move(item));
    }
    return value;
}

// 目录路径：孔数 + 逐部件选择（part_key/site_name/counts）。
bool parse_part_selections(
    const Json::Value& body,
    GenerateInventoryInput& output,
    std::string& error_code,
    std::string& error_message) {
    output.part_selections.clear();
    for (const auto& item : body["part_selections"]) {
        PartSelection selection;
        if (!item.isObject() || !non_empty_string(item, "part_key", selection.part_key)) {
            error_code = "invalid_inventory_part_selection";
            error_message = "构件部件选择必须包含部件键。";
            return false;
        }
        if (item.isMember("site_name")) {
            if (!item["site_name"].isString()) {
                error_code = "invalid_inventory_part_selection";
                error_message = "构件现场名称必须是文本。";
                return false;
            }
            selection.site_name = item["site_name"].asString();
        }
        if (item.isMember("counts")) {
            if (!item["counts"].isArray()) {
                error_code = "invalid_inventory_part_selection";
                error_message = "构件数量必须是整数数组。";
                return false;
            }
            for (const auto& count : item["counts"]) {
                if (!count.isInt() || count.asInt() < 0 || count.asInt() > 10000) {
                    error_code = "invalid_inventory_part_selection";
                    error_message = "构件数量必须是 0 到 10000 之间的整数。";
                    return false;
                }
                selection.counts.push_back(count.asInt());
            }
        }
        if (item.isMember("excluded_numbers")) {
            if (!item["excluded_numbers"].isArray()) {
                error_code = "invalid_inventory_part_selection";
                error_message = "构件排除编号必须是文本数组。";
                return false;
            }
            for (const auto& excluded : item["excluded_numbers"]) {
                if (!excluded.isString() || excluded.asString().empty()) {
                    error_code = "invalid_inventory_part_selection";
                    error_message = "构件排除编号必须是非空文本。";
                    return false;
                }
                selection.excluded_numbers.push_back(excluded.asString());
            }
        }
        output.part_selections.push_back(std::move(selection));
    }
    return true;
}

bool parse_generate_inventory_input(
    const Json::Value& body,
    GenerateInventoryInput& output,
    std::string& error_code,
    std::string& error_message) {
    if (!body.isObject() ||
        !non_empty_string(body, "standard_package_id", output.standard_package_id) ||
        !non_empty_string(body, "bridge_type_id", output.bridge_type_id)) {
        error_code = "invalid_inventory_generation_context";
        error_message = "规范包和桥型不能为空。";
        return false;
    }
    // template_id 仅旧 groups 路径需要；目录路径由部件目录承担。
    if (body.isMember("template_id")) {
        if (!body["template_id"].isString()) {
            error_code = "invalid_inventory_generation_context";
            error_message = "构件模板 ID 必须是文本。";
            return false;
        }
        output.template_id = body["template_id"].asString();
    }
    if (!body.isMember("span_count") || !body["span_count"].isInt() ||
        body["span_count"].asInt() < 0 || body["span_count"].asInt() > 1000) {
        error_code = "invalid_inventory_span_count";
        error_message = "跨数必须是 0 到 1000 之间的整数。";
        return false;
    }
    output.span_count = body["span_count"].asInt();
    output.input_quantities = body.isMember("input_quantities")
        ? body["input_quantities"] : Json::Value(Json::objectValue);
    if (!output.input_quantities.isObject()) {
        error_code = "invalid_inventory_quantities";
        error_message = "构件数量输入必须是对象。";
        return false;
    }

    if (!body.isMember("part_selections") || !body["part_selections"].isArray() ||
        body["part_selections"].empty()) {
        error_code = "inventory_part_selections_required";
        error_message = "至少需要选择一个构件生成部件。";
        return false;
    }
    return parse_part_selections(body, output, error_code, error_message);
}

}  // namespace bridge_report::inventory
