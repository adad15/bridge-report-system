#include "bridge_report/inventory/ComponentReplacePattern.hpp"

#include <cctype>

namespace bridge_report::inventory {
namespace {

constexpr char kWildcard = '*';

std::vector<std::string> split_on_wildcard(const std::string& value) {
    std::vector<std::string> segments;
    std::string current;
    for (const char character : value) {
        if (character == kWildcard) {
            segments.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(character);
    }
    segments.push_back(current);
    return segments;
}

}  // namespace

std::optional<ComponentReplacePattern> ComponentReplacePattern::compile(
    const std::string& find, const std::string& replace, std::string& error) {
    if (find.empty()) {
        error = "查找内容不能为空。";
        return std::nullopt;
    }
    auto find_segments = split_on_wildcard(find);
    auto replace_segments = split_on_wildcard(replace);
    const auto find_wildcards = find_segments.size() - 1;
    const auto replace_wildcards = replace_segments.size() - 1;
    if (replace_wildcards > find_wildcards) {
        // 替换里多出来的 * 无从取值，属于写错而非边界情形，直接挡住。
        error = "替换内容里的 * 比查找内容多（" + std::to_string(replace_wildcards) +
                " > " + std::to_string(find_wildcards) + "），多出的无从取值。";
        return std::nullopt;
    }

    ComponentReplacePattern pattern;
    pattern.find_segments_ = std::move(find_segments);
    pattern.replace_segments_ = std::move(replace_segments);
    return pattern;
}

std::optional<std::string> ComponentReplacePattern::apply(
    const std::string& text) const {
    // 手写扫描而不是正则：查找串里除 * 外一律按字面处理，用正则就得先替用户转义，
    // 漏一个元字符就会变成"看着像字面、实际按正则匹配"的隐蔽错误。
    std::vector<std::string> captures;
    std::size_t position = 0;

    // 首段必须从头字面命中（整串锚定）。
    const auto& first = find_segments_.front();
    if (text.compare(0, first.size(), first) != 0) return std::nullopt;
    position = first.size();

    for (std::size_t index = 1; index < find_segments_.size(); ++index) {
        // 通配位：先吃掉至少一位连续数字。
        const auto digits_start = position;
        while (position < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[position])) != 0) {
            ++position;
        }
        if (position == digits_start) return std::nullopt;
        captures.push_back(text.substr(digits_start, position - digits_start));

        const auto& segment = find_segments_[index];
        const bool is_last = index + 1 == find_segments_.size();
        if (is_last) {
            // 末段必须恰好收尾，否则整串没有命中。
            if (text.size() - position != segment.size() ||
                text.compare(position, segment.size(), segment) != 0) {
                return std::nullopt;
            }
            position = text.size();
            break;
        }
        if (text.compare(position, segment.size(), segment) != 0) return std::nullopt;
        position += segment.size();
    }
    if (position != text.size()) return std::nullopt;

    std::string result = replace_segments_.front();
    for (std::size_t index = 1; index < replace_segments_.size(); ++index) {
        // 第 k 段之后接第 k 个捕获；替换段数比 * 个数多一，最后一段无捕获可接。
        result += captures[index - 1];
        result += replace_segments_[index];
    }
    return result;
}

}  // namespace bridge_report::inventory
