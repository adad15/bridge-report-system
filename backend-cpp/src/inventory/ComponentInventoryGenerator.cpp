#include "bridge_report/inventory/ComponentInventoryGenerator.hpp"

#include <set>
#include <string>
#include <utility>

#include "bridge_report/inventory/ComponentPartCatalog.hpp"
#include "bridge_report/inventory/NumberingTemplate.hpp"

namespace bridge_report::inventory {
namespace {

// 目录路径：按《构件编号规则》模板 + 用户数量逐部件展开。
InventoryGenerationResult generate_from_catalog(const GenerateInventoryInput& input) {
    InventoryGenerationResult result;
    const auto& parts = component_parts();
    std::set<std::pair<std::string, std::string>> unique_numbers;
    int sort_order = 0;
    for (const auto& selection : input.part_selections) {
        const auto* part = find_part(parts, selection.part_key);
        if (part == nullptr) {
            result.entries.clear();
            result.error_code = "unknown_part_key";
            result.error_message = "构件部件不在梁式桥目录中：" + selection.part_key;
            return result;
        }
        const std::string name =
            selection.site_name.empty() ? part->default_name : selection.site_name;
        const auto numbers = expand(part->number_template_with(name, selection.counts),
                                    NumberingContext{input.span_count});
        for (const auto& generated : numbers) {
            if (generated.number.empty() ||
                !unique_numbers.emplace(name, generated.number).second) {
                result.entries.clear();
                result.error_code = "duplicate_inventory_component_number";
                result.error_message = "同一现场构件类型内生成了重复编号。";
                return result;
            }
            GeneratedInventoryEntry entry;
            entry.component_number = generated.number;
            entry.site_component_type = name;
            entry.site_name = name;
            entry.standard_component_category_id = part->standard_component_category_id;
            entry.structure_part = part->structure_part;
            if (!generated.location.empty()) entry.span_or_location = generated.location;
            entry.sort_order = ++sort_order;
            entry.generation_key = selection.part_key + ":" + std::to_string(entry.sort_order);
            result.entries.push_back(std::move(entry));
        }
    }
    return result;
}

std::string sequential_number(const GenerationGroupInput& group, const int index) {
    return group.number_prefix + std::to_string(index) + group.number_suffix;
}

std::string span_member_number(
    const GenerationGroupInput& group,
    const int span,
    const int member) {
    return group.number_prefix + std::to_string(span) + "-" +
        std::to_string(member) + group.number_suffix;
}

}  // namespace

InventoryGenerationResult generate_component_inventory(const GenerateInventoryInput& input) {
    // 新路径：按部件目录 + 模板生成；旧 groups 路径保留至清理任务。
    if (!input.part_selections.empty()) return generate_from_catalog(input);

    InventoryGenerationResult result;
    if (input.groups.empty()) {
        result.error_code = "inventory_groups_required";
        result.error_message = "至少需要一个构件生成分组。";
        return result;
    }

    std::set<std::pair<std::string, std::string>> unique_numbers;
    int sort_order = 0;
    for (std::size_t group_index = 0; group_index < input.groups.size(); ++group_index) {
        const auto& group = input.groups[group_index];
        if (group.site_component_type.empty() || group.site_name.empty() ||
            group.standard_component_category_id.empty() || group.structure_part.empty() ||
            group.quantity <= 0 || group.quantity > 10000) {
            result.entries.clear();
            result.error_code = "invalid_inventory_generation_group";
            result.error_message = "构件生成分组包含空字段或无效数量。";
            return result;
        }
        if (group.numbering_mode == NumberingMode::SpanMember &&
            (input.span_count <= 0 || input.span_count > 1000)) {
            result.entries.clear();
            result.error_code = "span_count_required_for_numbering";
            result.error_message = "按跨编号时必须填写有效跨数。";
            return result;
        }
        // 桥墩按墩位编号：两端为桥台，墩位数 = 跨数 - 1。
        if (group.numbering_mode == NumberingMode::PierLine &&
            (input.span_count <= 1 || input.span_count > 1000)) {
            result.entries.clear();
            result.error_code = "span_count_required_for_numbering";
            result.error_message = "按墩位编号时跨数至少为 2。";
            return result;
        }

        const int outer_count = group.numbering_mode == NumberingMode::SpanMember
            ? input.span_count
            : group.numbering_mode == NumberingMode::PierLine
                ? input.span_count - 1
                : 1;
        const int inner_count = group.quantity;
        if (static_cast<long long>(outer_count) * inner_count > 50000) {
            result.entries.clear();
            result.error_code = "inventory_generation_too_large";
            result.error_message = "单次生成的构件数量不能超过 50000。";
            return result;
        }
        for (int outer = 1; outer <= outer_count; ++outer) {
            for (int inner = 1; inner <= inner_count; ++inner) {
                GeneratedInventoryEntry entry;
                entry.component_number = group.numbering_mode == NumberingMode::Sequential
                    ? sequential_number(group, inner)
                    : span_member_number(group, outer, inner);
                if (entry.component_number.empty() ||
                    !unique_numbers.emplace(group.site_component_type,
                                            entry.component_number).second) {
                    result.entries.clear();
                    result.error_code = "duplicate_inventory_component_number";
                    result.error_message = "同一现场构件类型内生成了重复编号。";
                    return result;
                }
                entry.generation_key = std::to_string(group_index + 1) + ":" +
                    std::to_string(outer) + ":" + std::to_string(inner);
                entry.site_component_type = group.site_component_type;
                entry.site_name = group.site_name;
                entry.standard_component_category_id = group.standard_component_category_id;
                entry.structure_part = group.structure_part;
                entry.sort_order = ++sort_order;
                if (group.numbering_mode == NumberingMode::SpanMember) {
                    entry.span_or_location = "第" + std::to_string(outer) + "跨";
                } else if (group.numbering_mode == NumberingMode::PierLine) {
                    entry.span_or_location = "第" + std::to_string(outer) + "墩位";
                }
                result.entries.push_back(std::move(entry));
            }
        }
    }
    return result;
}

}  // namespace bridge_report::inventory
