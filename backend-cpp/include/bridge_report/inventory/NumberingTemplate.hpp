#pragma once

#include <string>
#include <vector>

namespace bridge_report::inventory {

enum class Placeholder { Span, Pier, Abutment, SupportLine, SpanSupport, Side, Count };

struct NumberingContext {
    int span_count{0};
};

struct PlaceValue {
    std::string token;     // 拼进编号的文本，如 "1"、"0#台"、"左"
    std::string location;  // 所属位置文案，无则空串
    bool operator==(const PlaceValue&) const = default;
};

// count 仅对 Placeholder::Count 有意义（1..count）；其余维用 span_count 派生。
std::vector<PlaceValue> placeholder_values(
    const NumberingContext& ctx, Placeholder placeholder, int count);

struct NumberingSlot {
    std::string token;        // 模板里的占位符，如 "{span}"
    Placeholder placeholder;
    int count{0};             // 仅 Placeholder::Count 用
};

struct NumberingTemplate {
    std::string pattern;                 // 如 "{span}-{c1}#梁"
    std::vector<NumberingSlot> slots;    // 出现顺序 = 外层到内层
};

struct GeneratedNumber {
    std::string number;
    std::string location;   // 最外层有位置的占位符的位置文案，无则空
};

std::vector<GeneratedNumber> expand(const NumberingTemplate& tpl, const NumberingContext& ctx);

}  // namespace bridge_report::inventory
