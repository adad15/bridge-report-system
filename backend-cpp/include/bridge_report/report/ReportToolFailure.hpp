#pragma once
#include <json/json.h>
#include <string>

namespace bridge_report::report {
// A completed HTTP response may still leave a COM writer alive.
inline bool report_tool_writer_uncertain(const Json::Value& payload) {
    const auto& detail=payload["detail"];
    if (!detail.isObject()) return false;
    if (detail["writer_uncertain"].isBool() && detail["writer_uncertain"].asBool()) return true;
    const auto code=detail["code"].isString()?detail["code"].asString():std::string{};
    const auto stage=detail["stage"].isString()?detail["stage"].asString():std::string{};
    return stage=="timeout" || code=="REPORT_FIELD_UPDATE_RECOVERY_REQUIRED";
}
}
