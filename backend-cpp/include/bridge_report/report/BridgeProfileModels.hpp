#pragma once

#include <optional>
#include <string>

#include <json/value.h>

namespace bridge_report::report {

/**
 * @brief 桥梁档案里描述这座桥本身的那些事实（设计 §8 第 1 章）。
 *
 * 报告 §1.1「桥梁概况」在正式报告里是三段叙述文字，不是一张两列表；这里每一项
 * 都是那几句话里的一个数或一个词。附录2「桥梁基本状况卡片」里对应的格子取的也是
 * 同一批值——同一个事实只存一处。
 *
 * **全部可空。** 档案是慢慢补起来的，缺哪一项就少写哪一句，绝不编数据
 * （设计 §14 第 5 条）。
 */
struct BridgeProfile {
    std::string bridge_id;
    std::string bridge_name;
    std::optional<std::string> business_code;
    std::optional<std::string> route_number;
    std::optional<std::string> route_name;
    std::optional<std::string> administrative_region;
    std::optional<std::string> station_mark;
    /// 桥位经度，WGS-84。地图底图用的 GCJ-02 在前端画图前转，接口上一律是 WGS-84。
    std::optional<double> longitude;
    std::optional<double> latitude;

    std::optional<std::string> bridge_type;
    std::optional<std::string> bridge_scale;
    std::optional<std::string> span_combination;
    std::optional<double> bridge_length_m;
    /// 含人行道的桥面总宽。与 carriageway_width_m 不是一回事。
    std::optional<double> bridge_width_m;
    std::optional<int> built_year;

    std::optional<double> skew_angle_deg;
    /// §1.1 印作「桥面净宽」，附录2 第 24 格印作「行车道宽」，同一个量。
    std::optional<double> carriageway_width_m;
    /// 单侧人行道宽度。两侧等宽时只存一个数。
    std::optional<double> sidewalk_width_m;

    std::optional<std::string> deck_pavement;
    std::optional<std::string> expansion_joint_type;
    /// 设伸缩缝的墩号，原样存用户录入的写法。
    std::optional<std::string> expansion_joint_piers;
    std::optional<std::string> bearing_type;

    std::optional<std::string> superstructure_form;
    std::optional<int> girders_per_span;
    std::optional<double> girder_height_m;

    std::optional<std::string> abutment_form;
    std::optional<std::string> pier_form;
    std::optional<std::string> foundation_form;

    std::optional<std::string> design_load;
    std::optional<std::string> design_org;
    std::optional<std::string> construction_org;
    /// 管养单位。与 supervision_org（监管单位）不是一回事。
    std::optional<std::string> maintenance_org;
    std::optional<std::string> supervision_org;

    Json::Value to_json() const;
};

/// 档案编辑的入参。空白字符串一律归一化为空，免得「  」被当成有值。
struct BridgeProfileInput {
    std::optional<std::string> business_code;
    std::optional<std::string> route_number;
    std::optional<std::string> route_name;
    std::optional<std::string> administrative_region;
    std::optional<std::string> station_mark;
    std::optional<double> longitude;
    std::optional<double> latitude;

    std::optional<std::string> bridge_type;
    std::optional<std::string> bridge_scale;
    std::optional<std::string> span_combination;
    std::optional<double> bridge_length_m;
    std::optional<double> bridge_width_m;
    std::optional<int> built_year;

    std::optional<double> skew_angle_deg;
    std::optional<double> carriageway_width_m;
    std::optional<double> sidewalk_width_m;

    std::optional<std::string> deck_pavement;
    std::optional<std::string> expansion_joint_type;
    std::optional<std::string> expansion_joint_piers;
    std::optional<std::string> bearing_type;

    std::optional<std::string> superstructure_form;
    std::optional<int> girders_per_span;
    std::optional<double> girder_height_m;

    std::optional<std::string> abutment_form;
    std::optional<std::string> pier_form;
    std::optional<std::string> foundation_form;

    std::optional<std::string> design_load;
    std::optional<std::string> design_org;
    std::optional<std::string> construction_org;
    std::optional<std::string> maintenance_org;
    std::optional<std::string> supervision_org;
};

/// 保存结果。除 Ok 外都要能向用户解释清楚，不是一句「保存失败」。
enum class BridgeProfileWriteStatus {
    Ok,
    BridgeNotFound,
    /// 片数、梁高、宽度必须为正，斜交角在 (0, 180]。数据库也拦，这里先给人话。
    MeasureOutOfRange,
};

}  // namespace bridge_report::report
