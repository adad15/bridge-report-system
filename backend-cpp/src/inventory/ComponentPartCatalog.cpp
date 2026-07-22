#include "bridge_report/inventory/ComponentPartCatalog.hpp"

#include <algorithm>

namespace bridge_report::inventory {
namespace {

// 按占位符在 pattern 中出现的先后重建有序 slots；Count 占位符依次消费传入的 counts。
NumberingTemplate assemble(const std::string& pattern, const std::vector<int>& counts) {
    NumberingTemplate tpl;
    tpl.pattern = pattern;
    struct Tok {
        std::size_t pos;
        std::string token;
        Placeholder placeholder;
        bool is_count;
    };
    std::vector<Tok> toks;
    const auto add = [&](const std::string& token, Placeholder placeholder, bool is_count) {
        const auto pos = pattern.find(token);
        if (pos != std::string::npos) toks.push_back({pos, token, placeholder, is_count});
    };
    add("{span}", Placeholder::Span, false);
    add("{pier}", Placeholder::Pier, false);
    add("{ab}", Placeholder::Abutment, false);
    add("{line}", Placeholder::SupportLine, false);
    add("{side}", Placeholder::Side, false);
    add("{c1}", Placeholder::Count, true);
    add("{c2}", Placeholder::Count, true);
    add("{c3}", Placeholder::Count, true);
    std::sort(toks.begin(), toks.end(), [](const Tok& a, const Tok& b) { return a.pos < b.pos; });
    std::size_t count_index = 0;
    for (const auto& tok : toks) {
        const int count =
            tok.is_count ? (count_index < counts.size() ? counts[count_index++] : 0) : 0;
        tpl.slots.push_back({tok.token, tok.placeholder, count});
    }
    return tpl;
}

}  // namespace

NumberingTemplate CatalogPart::number_template_with_counts(const std::vector<int>& counts) const {
    return assemble(number_template, counts);
}

NumberingTemplate CatalogPart::number_template_with(
    const std::string& name, const std::vector<int>& counts) const {
    std::string pattern = number_template;
    const auto pos = pattern.find("{name}");
    if (pos != std::string::npos) pattern.replace(pos, 6, name.empty() ? default_name : name);
    return assemble(pattern, counts);
}

const std::vector<CatalogPart>& component_parts() {
    static const std::vector<CatalogPart> parts = {
        // B. 共享 · 下部结构（梁 / 3 拱 / 斜拉；悬索桥另有特例下部见 G）
        {"lower.pier_column", "墩柱", "h21.component.lower.pier", "substructure",
         "{pier}-{c1}#{name}", {{"columns_per_pier", "每墩柱数"}}},
        {"lower.pier_cap", "盖梁", "h21.component.lower.pier", "substructure",
         "{pier}#墩{name}", {}},
        {"lower.tie_beam", "系梁", "h21.component.lower.pier", "substructure",
         "{pier}-{c1}#{name}", {{"tie_beams_per_pier", "每墩系梁数"}}},
        {"lower.abutment_body", "台", "h21.component.lower.abutment", "substructure",
         "{ab}#{name}", {}},
        {"lower.abutment_cap", "台帽", "h21.component.lower.abutment", "substructure",
         "{ab}#{name}", {}},
        {"lower.foundation", "基础", "h21.component.lower.foundation", "substructure",
         "{line}{name}", {}},
        {"lower.wing_wall", "翼墙", "h21.component.lower.wing_or_ear_wall", "substructure",
         "{ab}#台{side}侧{name}", {}},
        {"lower.cone_slope", "锥坡", "h21.component.lower.cone_or_protection_slope", "substructure",
         "{ab}#台{side}侧{name}", {}},
        {"lower.protection_slope", "护坡", "h21.component.lower.cone_or_protection_slope", "substructure",
         "{ab}#台{name}", {}},
        {"lower.regulation", "调治构造物", "h21.component.lower.regulation_structure", "substructure",
         "{c1}#{name}", {{"regulation_count", "数量"}}, true},

        // C. 共享 · 桥面系（全桥型一致）
        {"deck.pavement", "桥面铺装", "h21.component.deck.pavement", "deck_system",
         "{span}#跨{name}", {}},
        {"deck.expansion_joint", "伸缩缝", "h21.component.deck.expansion_joint", "deck_system",
         "{c1}#{name}", {{"expansion_joint_count", "伸缩缝数量"}}},
        {"deck.sidewalk", "人行道", "h21.component.deck.sidewalk", "deck_system",
         "{side}侧{name}", {}},
        {"deck.railing", "栏杆", "h21.component.deck.railing", "deck_system",
         "{side}侧{name}", {}},
        {"deck.drainage", "排水系统", "h21.component.deck.drainage", "deck_system",
         "{name}", {}},
        {"deck.lighting", "照明、标志", "h21.component.deck.lighting_signs", "deck_system",
         "{name}", {}},

        // D. 梁式桥 · 上部
        {"beam.girder", "梁", "h21.component.beam.upper_bearing", "superstructure",
         "{span}-{c1}#{name}", {{"girders_per_span", "每孔梁片数"}}},
        {"beam.wet_joint", "湿接缝", "h21.component.beam.upper_general", "superstructure",
         "{span}-{c1}#{name}", {{"joints_per_span", "每孔湿接缝条数"}}},
        {"beam.diaphragm", "横隔梁", "h21.component.beam.upper_general", "superstructure",
         "{span}-{c1}-{c2}#{name}", {{"gaps_per_span", "每孔梁间数"}, {"beams_per_gap", "每梁间道数"}}},

        // E. 拱桥三型 · 上部（provisional）
        {"arch.main_ring", "主拱圈", "h21.component.arch.main_ring", "superstructure",
         "{span}-{c1}#{name}", {{"rings_per_span", "每孔拱圈数"}}, true},
        {"arch.spandrel", "拱上结构", "h21.component.arch.spandrel", "superstructure",
         "{span}-{c1}#{name}", {{"spandrels_per_span", "每孔拱上结构数"}}, true},
        {"arch.deck_slab", "桥面板", "h21.component.arch.deck_slab", "superstructure",
         "{span}#跨{name}", {}, true},
        {"arch.segment", "拱片", "h21.component.arch.rigid_or_truss_segment", "superstructure",
         "{span}-{c1}#{name}", {{"segments_per_span", "每孔拱片数"}}, true},
        {"arch.transverse_link", "横向联结系", "h21.component.arch.transverse_link", "superstructure",
         "{span}-{c1}#{name}", {{"links_per_span", "每孔联结系数"}}, true},
        {"carch.rib", "拱肋", "h21.component.composite_arch.arch_rib", "superstructure",
         "{span}-{c1}#{name}", {{"ribs_per_span", "每孔拱肋数"}}, true},
        {"carch.transverse_link", "横向联结系", "h21.component.composite_arch.transverse_link",
         "superstructure", "{span}-{c1}#{name}", {{"links_per_span", "每孔联结系数"}}, true},
        {"carch.column", "立柱", "h21.component.composite_arch.column", "superstructure",
         "{span}-{c1}#{name}", {{"columns_per_span", "每孔立柱数"}}, true},
        {"carch.hanger", "吊杆", "h21.component.composite_arch.hanger", "superstructure",
         "{span}-{c1}#{name}", {{"hangers_per_span", "每孔吊杆数"}}, true},
        {"carch.tie_rod", "系杆", "h21.component.composite_arch.tie_rod", "superstructure",
         "{span}-{c1}#{name}", {{"tie_rods_per_span", "每孔系杆数"}}, true},
        {"carch.deck_slab_or_beam", "桥面板", "h21.component.composite_arch.deck_slab_or_beam",
         "superstructure", "{span}-{c1}#{name}", {{"slabs_per_span", "每孔桥面板数"}}, true},

        // F. 斜拉桥 · 上部（provisional）
        {"cs.main_girder", "主梁", "h21.component.cable_stayed.main_girder", "superstructure",
         "{span}-{c1}#{name}", {{"girders_per_span", "每孔主梁数"}}, true},
        {"cs.tower", "索塔", "h21.component.cable_stayed.tower", "superstructure",
         "{c1}#{name}", {{"tower_count", "索塔数量"}}, true},
        {"cs.cable", "斜拉索", "h21.component.cable_stayed.cable_system", "superstructure",
         "{c1}#{name}", {{"cable_count", "斜拉索数量"}}, true},

        // G. 悬索桥 · 上部 + 特例下部（provisional）
        {"sp.stiffening_girder", "加劲梁", "h21.component.suspension.stiffening_girder", "superstructure",
         "{span}-{c1}#{name}", {{"girders_per_span", "每孔加劲梁数"}}, true},
        {"sp.tower", "索塔", "h21.component.suspension.tower", "superstructure",
         "{c1}#{name}", {{"tower_count", "索塔数量"}}, true},
        {"sp.main_saddle", "主鞍", "h21.component.suspension.main_saddle", "superstructure",
         "{c1}#{name}", {{"saddle_count", "主鞍数量"}}, true},
        {"sp.main_cable", "主缆", "h21.component.suspension.main_cable", "superstructure",
         "{side}侧{name}", {}, true},
        {"sp.cable_clamp", "索夹", "h21.component.suspension.cable_clamp", "superstructure",
         "{c1}#{name}", {{"clamp_count", "索夹数量"}}, true},
        {"sp.hanger", "吊索", "h21.component.suspension.hanger", "superstructure",
         "{c1}#{name}", {{"hanger_count", "吊索数量"}}, true},
        {"sp.anchorage_rod", "锚杆", "h21.component.suspension.anchorage_rod", "superstructure",
         "{c1}#{name}", {{"rod_count", "锚杆数量"}}, true},
        {"sp.anchorage", "锚碇", "h21.component.suspension.anchorage", "substructure",
         "{c1}#{name}", {{"anchorage_count", "锚碇数量"}}, true},
        {"sp.tower_foundation", "索塔基础", "h21.component.suspension.tower_foundation", "substructure",
         "{c1}#{name}", {{"foundation_count", "索塔基础数量"}}, true},
        {"sp.splay_saddle", "散索鞍", "h21.component.suspension.splay_saddle", "substructure",
         "{c1}#{name}", {{"saddle_count", "散索鞍数量"}}, true},

        // H. 共享 · 支座（superstructure）——列在各桥型上部承重/一般构件之后。
        {"bearing.support", "支座", "h21.component.bearing", "superstructure",
         "{span}-{c1}-{c2}#{name}", {{"piers_per_span", "每孔墩数"}, {"bearings_per_pier", "每墩支座数"}}},
    };
    return parts;
}

const CatalogPart* find_part(const std::vector<CatalogPart>& parts, const std::string& key) {
    const auto it = std::find_if(parts.begin(), parts.end(),
        [&](const CatalogPart& part) { return part.part_key == key; });
    return it == parts.end() ? nullptr : &*it;
}

}  // namespace bridge_report::inventory
