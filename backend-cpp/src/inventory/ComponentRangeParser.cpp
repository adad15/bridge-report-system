#include "bridge_report/inventory/ComponentRangeParser.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

#include "bridge_report/inventory/ComponentMatcher.hpp"

namespace bridge_report::inventory {
namespace {

constexpr const char* kFullWidthWave = "～";
constexpr const char* kIdeographicSpace = "　";

std::string trim_endpoint(std::string value) {
    const auto ascii_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    while (!value.empty() && ascii_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && ascii_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    const std::string full_space = kIdeographicSpace;
    while (value.starts_with(full_space)) value.erase(0, full_space.size());
    while (value.ends_with(full_space)) value.erase(value.size() - full_space.size());
    return value;
}

struct Endpoint {
    std::string raw;
    std::string prefix;
    std::string suffix;
    long long number{0};
    std::size_t width{0};
};

std::optional<Endpoint> parse_endpoint(std::string raw) {
    raw = trim_endpoint(std::move(raw));
    if (raw.empty()) return std::nullopt;

    const auto last_digit = raw.find_last_of("0123456789");
    if (last_digit == std::string::npos) return std::nullopt;
    auto first_digit = last_digit;
    while (first_digit > 0
           && std::isdigit(static_cast<unsigned char>(raw[first_digit - 1])) != 0) {
        --first_digit;
    }

    const auto digits = raw.substr(first_digit, last_digit - first_digit + 1);
    try {
        Endpoint endpoint;
        endpoint.raw = raw;
        endpoint.prefix = raw.substr(0, first_digit);
        endpoint.suffix = raw.substr(last_digit + 1);
        endpoint.number = std::stoll(digits);
        endpoint.width = digits.size();
        return endpoint;
    } catch (...) {
        return std::nullopt;
    }
}

ComponentRangeParseResult fail(
    ComponentRangeParseStatus status,
    const std::string& source,
    std::string message
) {
    ComponentRangeParseResult result;
    result.status = status;
    result.source = source;
    result.message = std::move(message);
    return result;
}

}  // namespace

ComponentRangeParseResult parse_component_range(
    const std::string& value,
    std::size_t max_count
) {
    std::string normalized_separator = value;
    std::size_t offset = 0;
    const std::string full_wave = kFullWidthWave;
    while ((offset = normalized_separator.find(full_wave, offset)) != std::string::npos) {
        normalized_separator.replace(offset, full_wave.size(), "~");
        ++offset;
    }
    const auto separator_count = static_cast<std::size_t>(
        std::count(normalized_separator.begin(), normalized_separator.end(), '~'));

    if (separator_count == 0) {
        return fail(ComponentRangeParseStatus::NotRange, value, "构件编号不是范围。");
    }
    if (separator_count != 1) {
        return fail(ComponentRangeParseStatus::Invalid, value, "构件范围必须且只能包含一个分隔符。");
    }

    const auto separator = normalized_separator.find('~');
    const auto start = parse_endpoint(normalized_separator.substr(0, separator));
    const auto end = parse_endpoint(normalized_separator.substr(separator + 1));
    if (!start.has_value() || !end.has_value()) {
        return fail(ComponentRangeParseStatus::Invalid, value, "范围两端必须包含可展开的末位整数。");
    }
    if (normalize_component_number(start->prefix) != normalize_component_number(end->prefix)
        || normalize_component_number(start->suffix) != normalize_component_number(end->suffix)) {
        return fail(ComponentRangeParseStatus::Invalid, value, "范围两端的前缀和构件类型必须一致。");
    }
    if (start->number >= end->number) {
        return fail(ComponentRangeParseStatus::Invalid, value, "范围起点必须小于终点。");
    }

    const auto unsigned_count =
        static_cast<unsigned long long>(end->number - start->number) + 1ULL;
    if (max_count == 0 || unsigned_count > max_count
        || unsigned_count > static_cast<unsigned long long>(
            (std::numeric_limits<std::size_t>::max)())) {
        return fail(ComponentRangeParseStatus::LimitExceeded, value, "范围展开数量超过安全上限。");
    }

    ComponentRangeParseResult result;
    result.status = ComponentRangeParseStatus::Ok;
    result.source = value;
    result.first = start->raw;
    result.last = end->raw;
    result.numbers.reserve(static_cast<std::size_t>(unsigned_count));
    for (auto number = start->number; number <= end->number; ++number) {
        std::ostringstream rendered;
        rendered << start->prefix << std::setw(static_cast<int>(start->width))
                 << std::setfill('0') << number << start->suffix;
        result.numbers.push_back(rendered.str());
    }
    return result;
}

}  // namespace bridge_report::inventory
