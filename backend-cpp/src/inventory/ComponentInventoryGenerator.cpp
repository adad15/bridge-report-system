#include "bridge_report/inventory/ComponentInventoryGenerator.hpp"

#include <set>
#include <string>
#include <utility>

#include "bridge_report/inventory/ComponentPartCatalog.hpp"
#include "bridge_report/inventory/NumberingTemplate.hpp"

namespace bridge_report::inventory {

InventoryGenerationResult generate_component_inventory(const GenerateInventoryInput& input) {
    InventoryGenerationResult result;
    if (input.part_selections.empty()) {
        result.error_code = "inventory_part_selections_required";
        result.error_message = "至少需要选择一个构件生成部件。";
        return result;
    }

    const auto& parts = component_parts();
    std::set<std::pair<std::string, std::string>> unique_numbers;
    int sort_order = 0;
    for (const auto& selection : input.part_selections) {
        const auto* part = find_part(parts, selection.part_key);
        if (part == nullptr) {
            result.entries.clear();
            result.error_code = "unknown_part_key";
            result.error_message = "构件部件不在部件目录中：" + selection.part_key;
            return result;
        }
        const std::string name =
            selection.site_name.empty() ? part->default_name : selection.site_name;
        const auto numbers = expand(part->number_template_with(name, selection.counts),
                                    NumberingContext{input.span_count});
        // 用户在向导里去掉的位置（如某台没有翼墙）直接不生成；未命中的排除项说明前后端
        // 展开不一致，宁可报错也不静默忽略。
        const std::set<std::string> excluded(
            selection.excluded_numbers.begin(), selection.excluded_numbers.end());
        std::set<std::string> matched_exclusions;
        for (const auto& generated : numbers) {
            if (excluded.count(generated.number) != 0) {
                matched_exclusions.insert(generated.number);
                continue;
            }
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
        if (matched_exclusions.size() != excluded.size()) {
            result.entries.clear();
            result.error_code = "unknown_excluded_component_number";
            result.error_message = "排除的构件编号不在该部件生成结果中：" + selection.part_key;
            return result;
        }
    }
    return result;
}

}  // namespace bridge_report::inventory
