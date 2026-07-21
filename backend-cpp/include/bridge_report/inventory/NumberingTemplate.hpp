#pragma once

#include <string>
#include <vector>

namespace bridge_report::inventory {

enum class Placeholder { Span, Pier, Abutment, SupportLine, Side, Count };

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

}  // namespace bridge_report::inventory
