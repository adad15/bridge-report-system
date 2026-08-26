#include "bridge_report/review/ThreadCanonicalKey.hpp"

#include "bridge_report/review/ThreadSuggestions.hpp"

namespace bridge_report::review {

namespace {

// 字段分隔符取 US（0x1f）：病害类型和位置都是报告原文，不可能出现控制字符，
// 因此"类型+位置"永远撞不上"类型、空位置"。
constexpr char kFieldSeparator = '\x1f';

}  // namespace

std::string ThreadCanonicalKey::canonical_string() const {
    std::string result;
    result.reserve(bridge_component_id.size() + normalized_defect_type.size()
                   + normalized_defect_location.size() + 2);
    result += bridge_component_id;
    result += kFieldSeparator;
    result += normalized_defect_type;
    result += kFieldSeparator;
    result += normalized_defect_location;
    return result;
}

ThreadCanonicalKey make_thread_canonical_key(
    const std::string& bridge_component_id,
    const std::string& defect_type,
    const std::string& defect_location) {
    // normalize_suggestion_text 已经去掉全部 ASCII 空白，所以纯空白位置在这里自然落到
    // 空串——null 由调用方在取数时转成空串即可，三种"无位置"就此合一。
    return ThreadCanonicalKey{
        bridge_component_id,
        normalize_suggestion_text(defect_type),
        normalize_suggestion_text(defect_location),
    };
}

bool locations_overlap(
    const std::string& normalized_left,
    const std::string& normalized_right) {
    if (normalized_left.empty() || normalized_right.empty()) return false;
    if (normalized_left == normalized_right) return false;
    return normalized_left.find(normalized_right) != std::string::npos
        || normalized_right.find(normalized_left) != std::string::npos;
}

}  // namespace bridge_report::review
