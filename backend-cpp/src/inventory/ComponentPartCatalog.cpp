#include "bridge_report/inventory/BeamBridgePartCatalog.hpp"

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

NumberingTemplate BeamBridgePart::number_template_with_counts(const std::vector<int>& counts) const {
    return assemble(number_template, counts);
}

NumberingTemplate BeamBridgePart::number_template_with(
    const std::string& name, const std::vector<int>& counts) const {
    std::string pattern = number_template;
    const auto pos = pattern.find("{name}");
    if (pos != std::string::npos) pattern.replace(pos, 6, name.empty() ? default_name : name);
    return assemble(pattern, counts);
}

const std::vector<BeamBridgePart>& beam_bridge_parts() {
    static const std::vector<BeamBridgePart> parts = {
        {"beam.girder", "梁", "h21.component.beam.upper_bearing", "superstructure",
         "{span}-{c1}#{name}", {{"girders_per_span", "每孔梁片数"}}},
        {"beam.wet_joint", "湿接缝", "h21.component.beam.upper_general", "superstructure",
         "{span}-{c1}#{name}", {{"joints_per_span", "每孔湿接缝条数"}}},
        {"beam.diaphragm", "横隔梁", "h21.component.beam.upper_general", "superstructure",
         "{span}-{c1}-{c2}#{name}", {{"gaps_per_span", "每孔梁间数"}, {"beams_per_gap", "每梁间道数"}}},
        {"beam.bearing", "支座", "h21.component.bearing", "superstructure",
         "{span}-{c1}-{c2}#{name}", {{"piers_per_span", "每孔墩数"}, {"bearings_per_pier", "每墩支座数"}}},
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
    };
    return parts;
}

const BeamBridgePart* find_part(
    const std::vector<BeamBridgePart>& parts, const std::string& key) {
    const auto it = std::find_if(parts.begin(), parts.end(),
        [&](const BeamBridgePart& part) { return part.part_key == key; });
    return it == parts.end() ? nullptr : &*it;
}

}  // namespace bridge_report::inventory
