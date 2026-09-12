#include "bridge_report/standards/ComponentWeightTable.hpp"

#include <algorithm>

namespace bridge_report::standards {
namespace {

const StandardDefinition* definition(const StandardPackage& package, const std::string& id) {
    const auto found = package.definitions.find(id);
    return found == package.definitions.end() ? nullptr : &found->second;
}

/// 报告里的部位顺序：上部、下部、桥面系。权重集在包里的引用顺序不保证是这个。
int part_rank(StructurePart part) {
    switch (part) {
        case StructurePart::superstructure: return 1;
        case StructurePart::substructure: return 2;
        case StructurePart::deck_system: return 3;
    }
    return 4;
}

constexpr const char* kWeightSetPrefix = "h21.weight_set.";
// 结构层权重集按 structure_part 给权重，没有 component_id，不属于部件清单。
// 常量必须与包里的真实 id 一致：写成 "h21.weight_set.structure" 时这条排除
// 从来没生效过，只是下面的 component_id 过滤替它兜住了。
constexpr const char* kStructureWeightRule = "h21.weight_set.structure.default";

}  // namespace

std::string component_type_name(
    const StandardPackage& package, const std::string& component_type_id) {
    const auto* component = definition(package, component_type_id);
    if (component == nullptr || !component->payload["name"].isString()) return {};
    return component->payload["name"].asString();
}

std::vector<ComponentWeightEntry> component_weight_table(
    const StandardPackage& package, const std::string& bridge_type_id) {
    const StandardDefinition* profile = nullptr;
    for (const auto& [id, candidate] : package.definitions) {
        if (id.starts_with("h21.weight_profile.") &&
            candidate.payload["bridge_type_id"].isString() &&
            candidate.payload["bridge_type_id"].asString() == bridge_type_id) {
            profile = &candidate;
            break;
        }
    }
    if (profile == nullptr) return {};

    std::vector<ComponentWeightEntry> entries;
    for (const auto& reference : profile->references) {
        if (!reference.starts_with(kWeightSetPrefix) || reference == kStructureWeightRule) {
            continue;
        }
        const auto* rule = definition(package, reference);
        if (rule == nullptr || !rule->payload["weights"].isArray()) continue;
        const auto part = parse_structure_part(rule->payload["structure_part"].asString());
        if (!part.has_value()) continue;

        for (const auto& item : rule->payload["weights"]) {
            if (!item["component_id"].isString() || !item["value"].isNumeric()) continue;
            ComponentWeightEntry entry;
            entry.component_type_id = item["component_id"].asString();
            entry.structure_part = *part;
            entry.configured_weight = item["value"].asDouble();
            entry.component_type_name =
                component_type_name(package, entry.component_type_id);
            entries.push_back(std::move(entry));
        }
    }

    // 部位间按报告顺序，部位内保持规范表的原始次序。
    std::stable_sort(entries.begin(), entries.end(),
                     [](const ComponentWeightEntry& left, const ComponentWeightEntry& right) {
                         return part_rank(left.structure_part) < part_rank(right.structure_part);
                     });
    return entries;
}

}  // namespace bridge_report::standards
