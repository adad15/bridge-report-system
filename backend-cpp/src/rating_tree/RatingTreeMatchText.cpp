#include "bridge_report/rating_tree/RatingTreeMatchText.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>

namespace bridge_report::rating_tree {
namespace {

// 解码一个 UTF-8 码点，返回消费的字节数；非法序列按单字节跳过，保证不会死循环。
std::size_t decode_utf8(
    const std::string& value,
    const std::size_t offset,
    std::uint32_t& code_point) {
    const auto lead = static_cast<unsigned char>(value[offset]);
    std::size_t length = 1;
    std::uint32_t point = lead;
    if (lead >= 0xF0) {
        length = 4;
        point = lead & 0x07u;
    } else if (lead >= 0xE0) {
        length = 3;
        point = lead & 0x0Fu;
    } else if (lead >= 0xC0) {
        length = 2;
        point = lead & 0x1Fu;
    } else if (lead >= 0x80) {
        code_point = lead;
        return 1;
    }
    if (offset + length > value.size()) {
        code_point = lead;
        return 1;
    }
    for (std::size_t index = 1; index < length; ++index) {
        const auto continuation =
            static_cast<unsigned char>(value[offset + index]);
        if ((continuation & 0xC0u) != 0x80u) {
            code_point = lead;
            return 1;
        }
        point = (point << 6) | (continuation & 0x3Fu);
    }
    code_point = point;
    return length;
}

void append_utf8(std::string& target, const std::uint32_t code_point) {
    if (code_point < 0x80) {
        target.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800) {
        target.push_back(static_cast<char>(0xC0u | (code_point >> 6)));
        target.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    } else if (code_point < 0x10000) {
        target.push_back(static_cast<char>(0xE0u | (code_point >> 12)));
        target.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
        target.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    } else {
        target.push_back(static_cast<char>(0xF0u | (code_point >> 18)));
        target.push_back(static_cast<char>(0x80u | ((code_point >> 12) & 0x3Fu)));
        target.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
        target.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    }
}

// 统一到半角形式。全角 ASCII 区（U+FF01..U+FF5E）整体平移；其余是中文标点里
// 与半角同义的少数几个。破折号族一律归到 '-'，与 Word 里的空值占位符写法保持一致。
std::uint32_t fold_punctuation(const std::uint32_t code_point) {
    if (code_point >= 0xFF01 && code_point <= 0xFF5E) {
        return code_point - 0xFEE0;
    }
    switch (code_point) {
        case 0x3000:  // 表意空格
            return ' ';
        case 0x3001:  // 、
        case 0x3002:  // 。
            return ',';
        case 0x300C:  // 「
        case 0x300E:  // 『
        case 0x3010:  // 【
        case 0x3014:  // 〔
            return '[';
        case 0x300D:  // 」
        case 0x300F:  // 』
        case 0x3011:  // 】
        case 0x3015:  // 〕
            return ']';
        case 0x2014:  // —
        case 0x2015:  // ―
        case 0x2013:  // –
        case 0x2012:
        case 0x2010:
        case 0x2011:
        case 0x2212:  // −
            return '-';
        case 0x00B7:  // ·
            return ',';
        case 0x2018:
        case 0x2019:
            return '\'';
        case 0x201C:
        case 0x201D:
            return '"';
        default:
            return code_point;
    }
}

bool is_boundary_separator(const char ch) {
    switch (ch) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case ',':
        case ';':
        case ':':
        case '.':
        case '/':
        case '\\':
        case '-':
        case '_':
        case '|':
        case '~':
            return true;
        default:
            return false;
    }
}

bool is_segment_separator(const char ch) {
    return ch == ',' || ch == ';' || ch == '|' || ch == '\n' || ch == '\r';
}

}  // namespace

std::string normalize_match_key(const std::string& value) {
    std::string folded;
    folded.reserve(value.size());
    bool pending_space = false;
    bool has_content = false;
    for (std::size_t offset = 0; offset < value.size();) {
        std::uint32_t code_point = 0;
        offset += decode_utf8(value, offset, code_point);
        const auto normalized = fold_punctuation(code_point);
        if (normalized < 0x80 && std::isspace(static_cast<int>(normalized)) != 0) {
            // 连续空白合并成一个；首尾空白由 has_content / 末尾裁剪去掉。
            pending_space = has_content;
            continue;
        }
        if (pending_space) {
            folded.push_back(' ');
            pending_space = false;
        }
        if (normalized < 0x80) {
            folded.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(normalized))));
        } else {
            append_utf8(folded, normalized);
        }
        has_content = true;
    }

    // 只裁掉位于首尾的无语义分隔符：文字内部的 `/`、`-` 一律保留。
    std::size_t begin = 0;
    std::size_t end = folded.size();
    while (begin < end && is_boundary_separator(folded[begin])) ++begin;
    while (end > begin && is_boundary_separator(folded[end - 1])) --end;
    return folded.substr(begin, end - begin);
}

std::vector<std::string> split_match_segments(const std::string& value) {
    const auto normalized = normalize_match_key(value);
    std::vector<std::string> segments;
    std::string current;
    for (const char ch : normalized) {
        if (is_segment_separator(ch)) {
            auto trimmed = normalize_match_key(current);
            if (!trimmed.empty()) segments.push_back(std::move(trimmed));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    auto trimmed = normalize_match_key(current);
    if (!trimmed.empty()) segments.push_back(std::move(trimmed));
    return segments;
}

}  // namespace bridge_report::rating_tree
