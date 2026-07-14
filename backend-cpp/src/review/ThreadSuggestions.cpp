#include "bridge_report/review/ThreadSuggestions.hpp"

#include <algorithm>
#include <cctype>

namespace bridge_report::review {

namespace {

// 全角形式区 U+FF01-FF5E 与 ASCII 0x21-0x7E 逐一对应（差值 0xFEE0）。
// UTF-8 下该区编码为 EF BC 80-BF / EF BD 80-9E 三字节序列。
bool try_map_fullwidth(const std::string& input, std::size_t index, char& mapped, std::size_t& consumed) {
    if (index + 2 >= input.size()) {
        return false;
    }
    const auto byte0 = static_cast<unsigned char>(input[index]);
    const auto byte1 = static_cast<unsigned char>(input[index + 1]);
    const auto byte2 = static_cast<unsigned char>(input[index + 2]);
    if (byte0 != 0xEF) {
        return false;
    }
    unsigned int codepoint = 0;
    if (byte1 == 0xBC && byte2 >= 0x80 && byte2 <= 0xBF) {
        codepoint = 0xFF00u + (byte2 - 0x80u);
    } else if (byte1 == 0xBD && byte2 >= 0x80 && byte2 <= 0x9E) {
        codepoint = 0xFF40u + (byte2 - 0x80u);
    } else {
        return false;
    }
    if (codepoint < 0xFF01u || codepoint > 0xFF5Eu) {
        return false;
    }
    mapped = static_cast<char>(codepoint - 0xFEE0u);
    consumed = 3;
    return true;
}

// 常用 CJK 标点的固定映射：顿号 -> 逗号、句号 -> 点号；全角空格 U+3000 直接移除。
bool try_map_cjk_punct(const std::string& input, std::size_t index, char& mapped, std::size_t& consumed,
                       bool& removed) {
    if (index + 2 >= input.size()) {
        return false;
    }
    const auto byte0 = static_cast<unsigned char>(input[index]);
    const auto byte1 = static_cast<unsigned char>(input[index + 1]);
    const auto byte2 = static_cast<unsigned char>(input[index + 2]);
    if (byte0 != 0xE3 || byte1 != 0x80) {
        return false;
    }
    consumed = 3;
    if (byte2 == 0x80) {  // U+3000 全角空格
        removed = true;
        return true;
    }
    if (byte2 == 0x81) {  // U+3001 、
        mapped = ',';
        removed = false;
        return true;
    }
    if (byte2 == 0x82) {  // U+3002 。
        mapped = '.';
        removed = false;
        return true;
    }
    return false;
}

}  // namespace

std::string normalize_suggestion_text(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    std::size_t index = 0;
    while (index < input.size()) {
        const auto byte = static_cast<unsigned char>(input[index]);
        if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
            ++index;
            continue;
        }
        char mapped = 0;
        std::size_t consumed = 0;
        bool removed = false;
        if (try_map_fullwidth(input, index, mapped, consumed)) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(mapped))));
            index += consumed;
            continue;
        }
        if (try_map_cjk_punct(input, index, mapped, consumed, removed)) {
            if (!removed) {
                result.push_back(mapped);
            }
            index += consumed;
            continue;
        }
        if (byte < 0x80) {
            result.push_back(static_cast<char>(std::tolower(byte)));
            ++index;
            continue;
        }
        result.push_back(static_cast<char>(byte));
        ++index;
    }
    return result;
}

Json::Value suggest_threads(const ThreadSuggestionInput& observation, const Json::Value& threads) {
    struct Scored {
        double score{0.0};
        int latest_seen_year{0};
        std::string system_number;
        Json::Value payload;
    };

    const auto observation_type = normalize_suggestion_text(observation.defect_type);
    const auto observation_location = normalize_suggestion_text(observation.defect_location);

    std::vector<Scored> scored;
    if (threads.isArray()) {
        for (const auto& thread : threads) {
            if (!thread.isObject()) {
                continue;
            }
            const auto thread_type =
                normalize_suggestion_text(thread["defect_type"].isString() ? thread["defect_type"].asString() : "");
            const auto thread_location = normalize_suggestion_text(
                thread["defect_location"].isString() ? thread["defect_location"].asString() : "");

            const bool same_type = !thread_type.empty() && thread_type == observation_type;
            const bool location_exact =
                !thread_location.empty() && !observation_location.empty() && thread_location == observation_location;
            const bool location_contains = !location_exact && !thread_location.empty() && !observation_location.empty()
                && (thread_location.find(observation_location) != std::string::npos
                    || observation_location.find(thread_location) != std::string::npos);

            const double score = (same_type ? 2.0 : 0.0) + (location_exact ? 1.0 : 0.0)
                + (location_contains ? 0.5 : 0.0);
            if (score <= 0.0) {
                continue;
            }

            Scored item;
            item.score = score;
            item.latest_seen_year =
                thread["latest_seen_year"].isNumeric() ? thread["latest_seen_year"].asInt() : 0;
            item.system_number =
                thread["system_number"].isString() ? thread["system_number"].asString() : std::string();
            item.payload = thread;
            Json::Value match_basis;
            match_basis["same_component"] = true;  // 候选集已限定同构件
            match_basis["same_defect_type"] = same_type;
            match_basis["location_exact"] = location_exact;
            match_basis["location_contains"] = location_contains;
            item.payload["match_basis"] = match_basis;
            item.payload["suggestion_score"] = score;
            scored.push_back(std::move(item));
        }
    }

    std::stable_sort(scored.begin(), scored.end(), [](const Scored& left, const Scored& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        if (left.latest_seen_year != right.latest_seen_year) {
            return left.latest_seen_year > right.latest_seen_year;
        }
        return left.system_number < right.system_number;
    });

    Json::Value suggestions(Json::arrayValue);
    for (auto& item : scored) {
        suggestions.append(std::move(item.payload));
    }
    return suggestions;
}

}  // namespace bridge_report::review
