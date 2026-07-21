#include "bridge_report/inventory/NumberingTemplate.hpp"

namespace bridge_report::inventory {
namespace {

std::string replace_first(std::string value, const std::string& from, const std::string& to) {
    const auto pos = value.find(from);
    if (pos != std::string::npos) value.replace(pos, from.size(), to);
    return value;
}

}  // namespace

std::vector<PlaceValue> placeholder_values(
    const NumberingContext& ctx, const Placeholder placeholder, const int count) {
    std::vector<PlaceValue> values;
    const int n = ctx.span_count;
    switch (placeholder) {
        case Placeholder::Span:
            for (int k = 1; k <= n; ++k)
                values.push_back({std::to_string(k), "第" + std::to_string(k) + "孔"});
            break;
        case Placeholder::Pier:
            for (int k = 1; k <= n - 1; ++k)
                values.push_back({std::to_string(k), "第" + std::to_string(k) + "墩"});
            break;
        case Placeholder::Abutment:
            values.push_back({"0", "第0台"});
            if (n > 0) values.push_back({std::to_string(n), "第" + std::to_string(n) + "台"});
            break;
        case Placeholder::SupportLine:
            for (int k = 0; k <= n; ++k) {
                const std::string label =
                    std::to_string(k) + ((k == 0 || k == n) ? "#台" : "#墩");
                values.push_back({label, label});
            }
            break;
        case Placeholder::Side:
            values.push_back({"左", "左侧"});
            values.push_back({"右", "右侧"});
            break;
        case Placeholder::Count:
            for (int k = 1; k <= count; ++k) values.push_back({std::to_string(k), ""});
            break;
    }
    return values;
}

std::vector<GeneratedNumber> expand(const NumberingTemplate& tpl, const NumberingContext& ctx) {
    std::vector<GeneratedNumber> results{{tpl.pattern, ""}};
    for (const auto& slot : tpl.slots) {
        const auto values = placeholder_values(ctx, slot.placeholder, slot.count);
        std::vector<GeneratedNumber> next;
        next.reserve(results.size() * values.size());
        for (const auto& acc : results) {
            for (const auto& value : values) {
                GeneratedNumber item;
                item.number = replace_first(acc.number, slot.token, value.token);
                // 位置取最外层有位置的占位符：当前还没有位置且本维有位置时填。
                item.location = acc.location.empty() ? value.location : acc.location;
                next.push_back(std::move(item));
            }
        }
        results = std::move(next);
    }
    return results;
}

}  // namespace bridge_report::inventory
