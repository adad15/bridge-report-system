#include "bridge_report/inventory/ComponentCategoryLexicon.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <utility>

namespace bridge_report::inventory {
namespace {

// 删除从 open 到其后第一个 close 之间（含括号）的内容，处理多段。
std::string strip_between(std::string value, const std::string& open, const std::string& close) {
    std::size_t start = 0;
    while ((start = value.find(open, start)) != std::string::npos) {
        const auto end = value.find(close, start + open.size());
        if (end == std::string::npos) break;
        value.erase(start, end + close.size() - start);
    }
    return value;
}

// 名称归一化：去括号内示例/别名（全/半角）、去全角空格与 ASCII 空白。
std::string normalize_component_name(std::string value) {
    value = strip_between(std::move(value), "（", "）");
    value = strip_between(std::move(value), "(", ")");
    // 去全角空格。
    std::size_t pos = 0;
    while ((pos = value.find("　", pos)) != std::string::npos) value.erase(pos, std::string("　").size());
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), value.end());
    return value;
}

// 规范部件名（已去括号的干净写法；含个别别名如"桁架拱片"）→ 类别 id。
const std::vector<std::pair<std::string, std::string>>& name_category_pairs() {
    static const std::vector<std::pair<std::string, std::string>> pairs = {
        {"上部承重构件", "h21.component.beam.upper_bearing"},
        {"上部一般构件", "h21.component.beam.upper_general"},
        {"支座", "h21.component.bearing"},
        {"翼墙、耳墙", "h21.component.lower.wing_or_ear_wall"},
        {"锥坡、护坡", "h21.component.lower.cone_or_protection_slope"},
        {"桥墩", "h21.component.lower.pier"},
        {"桥台", "h21.component.lower.abutment"},
        {"墩台基础", "h21.component.lower.foundation"},
        {"调治构造物", "h21.component.lower.regulation_structure"},
        {"桥面铺装", "h21.component.deck.pavement"},
        {"伸缩缝装置", "h21.component.deck.expansion_joint"},
        {"人行道", "h21.component.deck.sidewalk"},
        {"栏杆、护栏", "h21.component.deck.railing"},
        {"排水系统", "h21.component.deck.drainage"},
        {"照明、标志", "h21.component.deck.lighting_signs"},
        {"主拱圈", "h21.component.arch.main_ring"},
        {"拱上结构", "h21.component.arch.spandrel"},
        {"桥面板", "h21.component.arch.deck_slab"},
        {"刚架拱片", "h21.component.arch.rigid_or_truss_segment"},
        {"桁架拱片", "h21.component.arch.rigid_or_truss_segment"},
        {"横向联结系", "h21.component.arch.transverse_link"},
        {"拱肋", "h21.component.composite_arch.arch_rib"},
        {"横向联结系", "h21.component.composite_arch.transverse_link"},
        {"立柱", "h21.component.composite_arch.column"},
        {"吊杆", "h21.component.composite_arch.hanger"},
        {"系杆", "h21.component.composite_arch.tie_rod"},
        {"桥面板", "h21.component.composite_arch.deck_slab_or_beam"},
        {"主梁", "h21.component.cable_stayed.main_girder"},
        {"索塔", "h21.component.cable_stayed.tower"},
        {"斜拉索系统", "h21.component.cable_stayed.cable_system"},
        {"加劲梁", "h21.component.suspension.stiffening_girder"},
        {"索塔", "h21.component.suspension.tower"},
        {"主鞍", "h21.component.suspension.main_saddle"},
        {"主缆", "h21.component.suspension.main_cable"},
        {"索夹", "h21.component.suspension.cable_clamp"},
        {"吊索及钢护筒", "h21.component.suspension.hanger"},
        {"锚杆", "h21.component.suspension.anchorage_rod"},
        {"锚碇", "h21.component.suspension.anchorage"},
        {"索塔基础", "h21.component.suspension.tower_foundation"},
        {"散索鞍", "h21.component.suspension.splay_saddle"},
    };
    return pairs;
}

const std::unordered_map<std::string, std::vector<std::string>>& lexicon() {
    static const auto index = [] {
        std::unordered_map<std::string, std::vector<std::string>> map;
        for (const auto& [name, category] : name_category_pairs()) {
            map[normalize_component_name(name)].push_back(category);
        }
        for (auto& [name, categories] : map) {
            std::sort(categories.begin(), categories.end());
            categories.erase(std::unique(categories.begin(), categories.end()), categories.end());
        }
        return map;
    }();
    return index;
}

}  // namespace

std::vector<std::string> resolve_component_categories(const std::string& part_name) {
    const auto key = normalize_component_name(part_name);
    if (key.empty()) return {};
    const auto& index = lexicon();
    const auto it = index.find(key);
    return it == index.end() ? std::vector<std::string>{} : it->second;
}

}  // namespace bridge_report::inventory
