#include "bridge_report/inventory/ComponentMatcher.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "bridge_report/inventory/ComponentCategoryLexicon.hpp"

namespace bridge_report::inventory {

const InventoryMapping* active_inventory_mapping(const InventoryEntry& entry) {
    const auto found = std::find_if(entry.mappings.begin(), entry.mappings.end(), [](const auto& mapping) {
        return mapping.is_active;
    });
    return found == entry.mappings.end() ? nullptr : &*found;
}

std::vector<const InventoryEntry*> usable_inventory_entries(const InventoryRevision& revision) {
    std::vector<const InventoryEntry*> result;
    for (const auto& entry : revision.entries) {
        if (entry.is_active && active_inventory_mapping(entry) != nullptr) result.push_back(&entry);
    }
    return result;
}

namespace {

std::string trim_ascii(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string();
}

std::string replace_all(std::string value, const std::string& from, const std::string& to) {
    std::size_t offset = 0;
    while ((offset = value.find(from, offset)) != std::string::npos) {
        value.replace(offset, from.size(), to);
        offset += to.size();
    }
    return value;
}

bool name_matches_confirmed_alias(
    const std::string& name,
    const InventoryEntry& entry,
    const std::vector<ConfirmedComponentAlias>& aliases
) {
    const auto trimmed = trim_ascii(name);
    return std::any_of(aliases.begin(), aliases.end(), [&](const auto& alias) {
        return alias.bridge_component_id == entry.bridge_component_id
            && trim_ascii(alias.alias_text) == trimmed;
    });
}

ComponentMatchResult candidate_result(
    ComponentMatchMethod method,
    const std::vector<const InventoryEntry*>& entries
) {
    ComponentMatchResult result;
    result.method = method;
    std::set<std::string> seen;
    for (const auto* entry : entries) {
        if (seen.insert(entry->bridge_component_id).second) {
            result.candidate_component_ids.push_back(entry->bridge_component_id);
        }
    }
    return result;
}

ComponentMatchResult matched_result(ComponentMatchMethod method, const InventoryEntry& entry) {
    ComponentMatchResult result;
    result.method = method;
    result.matched_entry = entry;
    result.matched_mapping = *active_inventory_mapping(entry);
    result.candidate_component_ids.push_back(entry.bridge_component_id);
    return result;
}

}  // namespace

std::string component_match_method_name(ComponentMatchMethod method) {
    switch (method) {
        case ComponentMatchMethod::Exact: return "exact";
        case ComponentMatchMethod::ConfirmedAlias: return "confirmed_alias";
        case ComponentMatchMethod::None: return "none";
    }
    return "none";
}

// 本函数是编号归一化的**权威实现**。前端为批量替换的预览做了逐步镜像：
// `frontend/src/review/binding/normalizeComponentNumber.ts`。改这里必须同步那边，
// 否则会出现"前端预览说能绑、后端却判无此编号"，且只在含全角字符的行上复现。
std::string normalize_component_number(const std::string& value) {
    auto normalized = trim_ascii(value);
    normalized = replace_all(std::move(normalized), "－", "-");
    normalized = replace_all(std::move(normalized), "–", "-");
    normalized = replace_all(std::move(normalized), "—", "-");
    normalized = replace_all(std::move(normalized), "＃", "#");
    normalized = replace_all(std::move(normalized), "　", "");
    normalized.erase(std::remove_if(normalized.begin(), normalized.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), normalized.end());
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    while (!normalized.empty() && normalized.back() == '#') normalized.pop_back();
    return normalized;
}

std::optional<BindableComponent> resolve_bindable_component(
    const InventoryRevision& revision,
    const std::string& part_name,
    const std::string& bridge_component_id
) {
    // 取**第一个**启用且 id 相符的条目；它若没有生效映射就到此为止，不再往后找
    // 同 id 的其它条目——同一版本内 bridge_component_id 本就唯一。
    const InventoryEntry* entry = nullptr;
    for (const auto& candidate : revision.entries) {
        if (candidate.is_active && candidate.bridge_component_id == bridge_component_id) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr) return std::nullopt;
    const auto* mapping = active_inventory_mapping(*entry);
    if (mapping == nullptr) return std::nullopt;

    // 部件名称解析不出类别时不设限：未知部件名交由上层判断，不在这里一刀切。
    const auto categories = resolve_component_categories(part_name);
    if (!categories.empty()
        && std::find(categories.begin(), categories.end(),
                     mapping->standard_component_category_id) == categories.end()) {
        return std::nullopt;
    }
    return BindableComponent{entry, mapping};
}

ComponentMatchResult match_defect_component(
    const DefectComponentText& defect,
    const InventoryRevision& revision,
    const std::vector<ConfirmedComponentAlias>& confirmed_aliases
) {
    const auto entries = usable_inventory_entries(revision);
    const bool inventory_confirmed =
        revision.status == "已确认" || revision.status == "confirmed";
    const auto normalized_number = normalize_component_number(defect.component_number);
    if (normalized_number.empty()) return {};

    // 主路径：部件类别（报告部件名称→对照表）+ 归一化编号（保留类型词）精确。
    const auto categories = resolve_component_categories(defect.component_name);
    if (!categories.empty()) {
        std::vector<const InventoryEntry*> hits;
        for (const auto* entry : entries) {
            const auto* mapping = active_inventory_mapping(*entry);
            if (mapping != nullptr
                && std::find(categories.begin(), categories.end(),
                             mapping->standard_component_category_id) != categories.end()
                && normalize_component_number(entry->component_number) == normalized_number) {
                hits.push_back(entry);
            }
        }
        if (hits.size() == 1 && inventory_confirmed) {
            return matched_result(ComponentMatchMethod::Exact, *hits.front());
        }
        if (!hits.empty()) return candidate_result(ComponentMatchMethod::Exact, hits);
    }

    // 兜底：人工确认别名（归一化编号 + 别名文本 == 报告部件名称）。
    std::vector<const InventoryEntry*> aliases;
    for (const auto* entry : entries) {
        if (normalize_component_number(entry->component_number) == normalized_number
            && name_matches_confirmed_alias(defect.component_name, *entry, confirmed_aliases)) {
            aliases.push_back(entry);
        }
    }
    if (aliases.size() == 1 && inventory_confirmed) {
        return matched_result(ComponentMatchMethod::ConfirmedAlias, *aliases.front());
    }
    if (!aliases.empty()) return candidate_result(ComponentMatchMethod::ConfirmedAlias, aliases);

    return {};
}

}  // namespace bridge_report::inventory
