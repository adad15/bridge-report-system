#include "bridge_report/db/BridgeProfileRepository.hpp"

#include <utility>

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>

namespace bridge_report::db {
namespace {

/// 读写共用一份列清单，省得两处各抄一遍再慢慢漂移。
constexpr const char* kProfileColumns =
    "id::text as bridge_id, bridge_name, business_code, route_number, route_name, "
    "administrative_region, station_mark, longitude, latitude, "
    "bridge_type, bridge_scale, span_combination, "
    "bridge_length_m, bridge_width_m, built_year, skew_angle_deg, carriageway_width_m, "
    "sidewalk_width_m, deck_pavement, expansion_joint_type, expansion_joint_piers, "
    "bearing_type, superstructure_form, girders_per_span, girder_height_m, "
    "abutment_form, pier_form, foundation_form, design_load, design_org, "
    "construction_org, maintenance_org, supervision_org";

std::optional<std::string> optional_text(const drogon::orm::Row& row, const char* column) {
    const auto field = row[column];
    if (field.isNull()) return std::nullopt;
    return field.as<std::string>();
}

std::optional<double> optional_double(const drogon::orm::Row& row, const char* column) {
    const auto field = row[column];
    if (field.isNull()) return std::nullopt;
    return field.as<double>();
}

std::optional<int> optional_int(const drogon::orm::Row& row, const char* column) {
    const auto field = row[column];
    if (field.isNull()) return std::nullopt;
    return field.as<int>();
}

report::BridgeProfile read_profile(const drogon::orm::Row& row) {
    report::BridgeProfile profile;
    profile.bridge_id = row["bridge_id"].as<std::string>();
    profile.bridge_name = row["bridge_name"].as<std::string>();
    profile.business_code = optional_text(row, "business_code");
    profile.route_number = optional_text(row, "route_number");
    profile.route_name = optional_text(row, "route_name");
    profile.administrative_region = optional_text(row, "administrative_region");
    profile.station_mark = optional_text(row, "station_mark");
    profile.longitude = optional_double(row, "longitude");
    profile.latitude = optional_double(row, "latitude");

    profile.bridge_type = optional_text(row, "bridge_type");
    profile.bridge_scale = optional_text(row, "bridge_scale");
    profile.span_combination = optional_text(row, "span_combination");
    profile.bridge_length_m = optional_double(row, "bridge_length_m");
    profile.bridge_width_m = optional_double(row, "bridge_width_m");
    profile.built_year = optional_int(row, "built_year");

    profile.skew_angle_deg = optional_double(row, "skew_angle_deg");
    profile.carriageway_width_m = optional_double(row, "carriageway_width_m");
    profile.sidewalk_width_m = optional_double(row, "sidewalk_width_m");

    profile.deck_pavement = optional_text(row, "deck_pavement");
    profile.expansion_joint_type = optional_text(row, "expansion_joint_type");
    profile.expansion_joint_piers = optional_text(row, "expansion_joint_piers");
    profile.bearing_type = optional_text(row, "bearing_type");

    profile.superstructure_form = optional_text(row, "superstructure_form");
    profile.girders_per_span = optional_int(row, "girders_per_span");
    profile.girder_height_m = optional_double(row, "girder_height_m");

    profile.abutment_form = optional_text(row, "abutment_form");
    profile.pier_form = optional_text(row, "pier_form");
    profile.foundation_form = optional_text(row, "foundation_form");

    profile.design_load = optional_text(row, "design_load");
    profile.design_org = optional_text(row, "design_org");
    profile.construction_org = optional_text(row, "construction_org");
    profile.maintenance_org = optional_text(row, "maintenance_org");
    profile.supervision_org = optional_text(row, "supervision_org");
    return profile;
}

/// 文本入参落库前的归一化：空白等于没填。
///
/// 不这么做的话，用户清空一个输入框留下的那个空格会被当成有值，报告里就会印出
/// 「桥面铺装采用 」这种半截话。
std::string text_param(const std::optional<std::string>& value) {
    if (!value.has_value()) return {};
    const auto first = value->find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value->find_last_not_of(" \t\r\n");
    return value->substr(first, last - first + 1);
}

/// 数值入参。空值走一个空字符串，SQL 侧用 nullif('')::numeric 还原成 NULL。
std::string number_param(const std::optional<double>& value) {
    return value.has_value() ? std::to_string(*value) : std::string();
}

std::string number_param(const std::optional<int>& value) {
    return value.has_value() ? std::to_string(*value) : std::string();
}

}  // namespace

BridgeProfileRepository::BridgeProfileRepository(drogon::orm::DbClientPtr client)
    : client_(std::move(client)) {}

std::optional<report::BridgeProfile> BridgeProfileRepository::find(
    const std::string& bridge_id) const {
    const auto rows = client_->execSqlSync(
        std::string("select ") + kProfileColumns + " from bridges where id = $1::uuid",
        bridge_id);
    if (rows.empty()) return std::nullopt;
    return read_profile(rows[0]);
}

report::BridgeProfileWriteStatus BridgeProfileRepository::save(
    const std::string& bridge_id, const report::BridgeProfileInput& input) const {
    try {
        const auto rows = client_->execSqlSync(
            "update bridges set "
            " business_code = nullif($2, ''), "
            " route_number = nullif($3, ''), "
            " route_name = nullif($4, ''), "
            " administrative_region = nullif($5, ''), "
            " station_mark = nullif($6, ''), "
            " longitude = nullif($31, '')::numeric, "
            " latitude = nullif($32, '')::numeric, "
            " bridge_type = nullif($7, ''), "
            " bridge_scale = nullif($8, ''), "
            " span_combination = nullif($9, ''), "
            " bridge_length_m = nullif($10, '')::numeric, "
            " bridge_width_m = nullif($11, '')::numeric, "
            " built_year = nullif($12, '')::integer, "
            " skew_angle_deg = nullif($13, '')::numeric, "
            " carriageway_width_m = nullif($14, '')::numeric, "
            " sidewalk_width_m = nullif($15, '')::numeric, "
            " deck_pavement = nullif($16, ''), "
            " expansion_joint_type = nullif($17, ''), "
            " expansion_joint_piers = nullif($18, ''), "
            " bearing_type = nullif($19, ''), "
            " superstructure_form = nullif($20, ''), "
            " girders_per_span = nullif($21, '')::integer, "
            " girder_height_m = nullif($22, '')::numeric, "
            " abutment_form = nullif($23, ''), "
            " pier_form = nullif($24, ''), "
            " foundation_form = nullif($25, ''), "
            " design_load = nullif($26, ''), "
            " design_org = nullif($27, ''), "
            " construction_org = nullif($28, ''), "
            " maintenance_org = nullif($29, ''), "
            " supervision_org = nullif($30, ''), "
            " updated_at = now() "
            "where id = $1::uuid returning id",
            bridge_id,
            text_param(input.business_code), text_param(input.route_number),
            text_param(input.route_name), text_param(input.administrative_region),
            text_param(input.station_mark), text_param(input.bridge_type),
            text_param(input.bridge_scale), text_param(input.span_combination),
            number_param(input.bridge_length_m), number_param(input.bridge_width_m),
            number_param(input.built_year), number_param(input.skew_angle_deg),
            number_param(input.carriageway_width_m), number_param(input.sidewalk_width_m),
            text_param(input.deck_pavement), text_param(input.expansion_joint_type),
            text_param(input.expansion_joint_piers), text_param(input.bearing_type),
            text_param(input.superstructure_form), number_param(input.girders_per_span),
            number_param(input.girder_height_m), text_param(input.abutment_form),
            text_param(input.pier_form), text_param(input.foundation_form),
            text_param(input.design_load), text_param(input.design_org),
            text_param(input.construction_org), text_param(input.maintenance_org),
            text_param(input.supervision_org),
            number_param(input.longitude), number_param(input.latitude));
        if (rows.empty()) return report::BridgeProfileWriteStatus::BridgeNotFound;
        return report::BridgeProfileWriteStatus::Ok;
    } catch (const drogon::orm::DrogonDbException&) {
        // 片数、梁高、宽度为正，斜交角在 (0, 180]，经纬度在 ±180 / ±90 ——
        // 数据库的 check 约束挡下来了。
        // 这里翻成一个能向用户解释的状态，而不是笼统的「保存失败」。
        return report::BridgeProfileWriteStatus::MeasureOutOfRange;
    }
}

}  // namespace bridge_report::db
