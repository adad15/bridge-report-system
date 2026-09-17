#include "bridge_report/report/BridgeMediaModels.hpp"

#include <algorithm>

namespace bridge_report::report {

bool is_bridge_media_slot(std::string_view code) {
    return std::any_of(kBridgeMediaSlots.begin(), kBridgeMediaSlots.end(),
                       [code](const BridgeMediaSlot& slot) { return slot.code == code; });
}

std::string_view bridge_media_slot_label(std::string_view code) {
    const auto found = std::find_if(kBridgeMediaSlots.begin(), kBridgeMediaSlots.end(),
                                    [code](const BridgeMediaSlot& slot) { return slot.code == code; });
    return found == kBridgeMediaSlots.end() ? std::string_view{} : found->label;
}

Json::Value BridgeMedia::to_json() const {
    Json::Value json;
    json["id"] = id;
    json["bridge_id"] = bridge_id;
    json["slot"] = slot;
    json["slot_label"] = std::string(bridge_media_slot_label(slot));
    json["original_file_name"] = original_file_name;
    json["file_extension"] = file_extension;
    json["file_size_bytes"] = static_cast<Json::UInt64>(file_size_bytes);
    json["source"] = source;
    json["created_at"] = created_at;
    json["updated_at"] = updated_at;
    // 存储路径不下发：前端按 id 取图，不需要也不应该知道归档里的位置。
    return json;
}

Json::Value bridge_media_list_json(const std::vector<BridgeMedia>& items) {
    Json::Value array(Json::arrayValue);
    for (const auto& item : items) array.append(item.to_json());
    return array;
}

}  // namespace bridge_report::report
