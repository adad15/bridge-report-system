#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json/value.h>

namespace bridge_report::report {

/**
 * @brief 桥梁图件的槽位。
 *
 * 报告 §1.1 里每张图有固定的图号和题注，所以槽位是一份封闭清单，一个槽位一张图。
 * 这些代码同时出现在数据库的 check 约束、报告契约和界面上，三处必须一致。
 */
struct BridgeMediaSlot {
    std::string_view code;
    /// 界面和报告题注里印的名字。
    std::string_view label;
};

inline constexpr std::array<BridgeMediaSlot, 6> kBridgeMediaSlots{{
    {"LOCATION_MAP", "地理位置图"},
    {"LAYOUT_DRAWING", "桥型布置图"},
    {"CROSS_SECTION", "横断面图"},
    {"OVERVIEW_PHOTO", "桥梁全貌照片"},
    {"DECK_PHOTO", "桥面照片"},
    {"UNDERSIDE_PHOTO", "桥下照片"},
}};

/// 槽位代码是否在清单里。不在就拒绝，不让未知代码写进库。
bool is_bridge_media_slot(std::string_view code);

/// 槽位对应的中文名；代码不认识时返回空。
std::string_view bridge_media_slot_label(std::string_view code);

/// 一张已归档的图件。
struct BridgeMedia {
    std::string id;
    std::string bridge_id;
    std::string slot;
    std::string archived_file_id;
    std::string original_file_name;
    std::string storage_relative_path;
    std::string file_extension;
    std::uintmax_t file_size_bytes{0};
    std::string source;
    std::string created_at;
    std::string updated_at;

    Json::Value to_json() const;
};

/// 新上传的一张图件：内容已经落盘，这里只记数据库要写的那几项。
struct BridgeMediaInput {
    std::string slot;
    std::string original_file_name;
    std::string storage_relative_path;
    std::string file_extension;
    std::uintmax_t file_size_bytes{0};
    std::string file_hash;
    std::string source;
};

enum class BridgeMediaWriteStatus {
    Ok,
    BridgeNotFound,
    UnknownSlot,
};

/// 被替换掉的旧图，交给调用方在事务落定之后删文件。
struct BridgeMediaReplacement {
    std::optional<std::string> archived_file_id;
    std::optional<std::string> storage_relative_path;
    std::optional<std::string> file_hash;
};

Json::Value bridge_media_list_json(const std::vector<BridgeMedia>& items);

}  // namespace bridge_report::report
