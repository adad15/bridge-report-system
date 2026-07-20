#include "bridge_report/inventory/ComponentInventoryModels.hpp"

#include <array>
#include <string_view>

namespace bridge_report::inventory {
namespace {

bool non_empty_string(const Json::Value& value, const char* key, std::string& output) {
    if (!value.isMember(key) || !value[key].isString()) return false;
    output = value[key].asString();
    return !output.empty();
}

bool valid_structure_part(const std::string& value) {
    static constexpr std::array<std::string_view, 5> values{
        "superstructure", "substructure", "deck_system", "overall", "other"};
    for (const auto candidate : values) {
        if (value == candidate) return true;
    }
    return false;
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

std::optional<NumberingMode> parse_numbering_mode(const std::string& value) {
    if (value == "span_member") return NumberingMode::SpanMember;
    if (value == "pier_line") return NumberingMode::PierLine;
    if (value == "sequential") return NumberingMode::Sequential;
    return std::nullopt;
}

std::string to_string(const NumberingMode mode) {
    switch (mode) {
        case NumberingMode::SpanMember: return "span_member";
        case NumberingMode::PierLine: return "pier_line";
        case NumberingMode::Sequential: return "sequential";
    }
    return "sequential";
}

Json::Value inventory_revision_json(const InventoryRevision& revision) {
    Json::Value value;
    value["id"] = revision.id;
    value["bridge_id"] = revision.bridge_id;
    value["revision_number"] = revision.revision_number;
    value["status"] = revision.status;
    value["baseline_revision_id"] = revision.baseline_revision_id.has_value()
        ? Json::Value(*revision.baseline_revision_id) : Json::Value(Json::nullValue);
    value["confirmed_at"] = revision.confirmed_at.has_value()
        ? Json::Value(*revision.confirmed_at) : Json::Value(Json::nullValue);
    value["entries"] = Json::Value(Json::arrayValue);
    for (const auto& entry : revision.entries) {
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
        value["entries"].append(std::move(item));
    }
    return value;
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

bool parse_generate_inventory_input(
    const Json::Value& body,
    GenerateInventoryInput& output,
    std::string& error_code,
    std::string& error_message) {
    if (!body.isObject() ||
        !non_empty_string(body, "standard_package_id", output.standard_package_id) ||
        !non_empty_string(body, "template_id", output.template_id) ||
        !non_empty_string(body, "bridge_type_id", output.bridge_type_id)) {
        error_code = "invalid_inventory_generation_context";
        error_message = "规范包、构件模板和桥型不能为空。";
        return false;
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
    if (!body.isMember("groups") || !body["groups"].isArray() || body["groups"].empty()) {
        error_code = "inventory_groups_required";
        error_message = "至少需要一个构件生成分组。";
        return false;
    }

    output.groups.clear();
    for (const auto& item : body["groups"]) {
        GenerationGroupInput group;
        std::string numbering;
        if (!item.isObject() ||
            !non_empty_string(item, "site_component_type", group.site_component_type) ||
            !non_empty_string(item, "site_name", group.site_name) ||
            !non_empty_string(item, "standard_component_category_id",
                              group.standard_component_category_id) ||
            !non_empty_string(item, "structure_part", group.structure_part) ||
            !non_empty_string(item, "numbering_mode", numbering) ||
            !item.isMember("quantity") || !item["quantity"].isInt()) {
            error_code = "invalid_inventory_generation_group";
            error_message = "构件生成分组字段不完整。";
            return false;
        }
        const auto mode = parse_numbering_mode(numbering);
        if (!mode.has_value() || !valid_structure_part(group.structure_part) ||
            item["quantity"].asInt() <= 0 || item["quantity"].asInt() > 10000) {
            error_code = "invalid_inventory_generation_group";
            error_message = "构件数量、编号方式或内部结构部位无效。";
            return false;
        }
        group.numbering_mode = *mode;
        group.quantity = item["quantity"].asInt();
        if (!non_empty_string(item, "quantity_key", group.quantity_key)) {
            error_code = "invalid_inventory_quantity_key";
            error_message = "构件生成分组必须关联规范模板中的数量项。";
            return false;
        }
        if (item.isMember("number_prefix")) {
            if (!item["number_prefix"].isString()) {
                error_code = "invalid_inventory_number_affix";
                error_message = "编号前缀必须是文本。";
                return false;
            }
            group.number_prefix = item["number_prefix"].asString();
        }
        if (item.isMember("number_suffix")) {
            if (!item["number_suffix"].isString()) {
                error_code = "invalid_inventory_number_affix";
                error_message = "编号后缀必须是文本。";
                return false;
            }
            group.number_suffix = item["number_suffix"].asString();
        }
        output.groups.push_back(std::move(group));
    }
    return true;
}

}  // namespace bridge_report::inventory
