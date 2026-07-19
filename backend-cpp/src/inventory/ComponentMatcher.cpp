#include "bridge_report/inventory/ComponentMatcher.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace bridge_report::inventory {
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

const InventoryMapping* active_mapping(const InventoryEntry& entry) {
    const auto found = std::find_if(entry.mappings.begin(), entry.mappings.end(), [](const auto& mapping) {
        return mapping.is_active;
    });
    return found == entry.mappings.end() ? nullptr : &*found;
}

bool name_matches_entry(const std::string& name, const InventoryEntry& entry) {
    const auto trimmed = trim_ascii(name);
    return trimmed == trim_ascii(entry.site_component_type) || trimmed == trim_ascii(entry.site_name);
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

std::vector<const InventoryEntry*> usable_entries(const InventoryRevision& revision) {
    std::vector<const InventoryEntry*> result;
    for (const auto& entry : revision.entries) {
        if (entry.is_active && active_mapping(entry) != nullptr) result.push_back(&entry);
    }
    return result;
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
    result.matched_mapping = *active_mapping(entry);
    result.candidate_component_ids.push_back(entry.bridge_component_id);
    return result;
}

}  // namespace

std::string component_match_method_name(ComponentMatchMethod method) {
    switch (method) {
        case ComponentMatchMethod::Exact: return "exact";
        case ComponentMatchMethod::ConfirmedAlias: return "confirmed_alias";
        case ComponentMatchMethod::NormalizedCandidate: return "normalized_candidate";
        case ComponentMatchMethod::None: return "none";
    }
    return "none";
}

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

ComponentMatchResult match_defect_component(
    const DefectComponentText& defect,
    const InventoryRevision& revision,
    const std::vector<ConfirmedComponentAlias>& confirmed_aliases
) {
    const auto entries = usable_entries(revision);
    std::vector<const InventoryEntry*> exact;
    for (const auto* entry : entries) {
        if (trim_ascii(entry->component_number) == trim_ascii(defect.component_number)
            && name_matches_entry(defect.component_name, *entry)) {
            exact.push_back(entry);
        }
    }
    const bool inventory_confirmed =
        revision.status == "已确认" || revision.status == "confirmed";
    if (exact.size() == 1 && inventory_confirmed) {
        return matched_result(ComponentMatchMethod::Exact, *exact.front());
    }
    if (!exact.empty()) return candidate_result(ComponentMatchMethod::Exact, exact);

    std::vector<const InventoryEntry*> aliases;
    for (const auto* entry : entries) {
        if (trim_ascii(entry->component_number) == trim_ascii(defect.component_number)
            && name_matches_confirmed_alias(defect.component_name, *entry, confirmed_aliases)) {
            aliases.push_back(entry);
        }
    }
    if (aliases.size() == 1 && inventory_confirmed) {
        return matched_result(ComponentMatchMethod::ConfirmedAlias, *aliases.front());
    }
    if (!aliases.empty()) return candidate_result(ComponentMatchMethod::ConfirmedAlias, aliases);

    const auto normalized_number = normalize_component_number(defect.component_number);
    if (normalized_number.empty()) return {};
    std::vector<const InventoryEntry*> normalized;
    for (const auto* entry : entries) {
        const bool name_matches = name_matches_entry(defect.component_name, *entry)
            || name_matches_confirmed_alias(defect.component_name, *entry, confirmed_aliases);
        if (name_matches && normalize_component_number(entry->component_number) == normalized_number) {
            normalized.push_back(entry);
        }
    }
    return normalized.empty()
        ? ComponentMatchResult{}
        : candidate_result(ComponentMatchMethod::NormalizedCandidate, normalized);
}

}  // namespace bridge_report::inventory
