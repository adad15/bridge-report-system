#include "bridge_report/inventory/NumberingTemplate.hpp"

namespace bridge_report::inventory {

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

}  // namespace bridge_report::inventory
