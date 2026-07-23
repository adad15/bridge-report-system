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
        // 一孔恒有两个支承（左右各一）。《构件编号规则》第10条：支座在桥孔和桥墩编号的
        // 基础上自右至左编号，故两个支承记作 1、2，1 为右侧。孔数不参与，是几何常量。
        case Placeholder::SpanSupport:
            values.push_back({"1", "第1号墩"});
            values.push_back({"2", "第2号墩"});
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
