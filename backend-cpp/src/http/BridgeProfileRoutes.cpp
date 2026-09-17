#include "bridge_report/http/BridgeProfileRoutes.hpp"

#include <optional>
#include <string>

#include "bridge_report/db/BridgeProfileRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {
namespace {

std::optional<db::AuthUser> require_user(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback) {
    auto user = authenticate_request(db_client, request);
    if (!user.has_value()) respond_unauthorized(callback);
    return user;
}

void respond_bridge_not_found(const HttpCallback& callback) {
    respond_json(callback, make_error_body("bridge_not_found", "桥梁不存在。"),
                 drogon::k404NotFound);
}

std::optional<std::string> read_text(const Json::Value& body, const char* key) {
    if (!body.isMember(key) || body[key].isNull()) return std::nullopt;
    if (!body[key].isString()) return std::nullopt;
    return body[key].asString();
}

/// 数字字段：接受数字，也接受界面上常见的数字字符串。空串等于没填。
std::optional<double> read_number(const Json::Value& body, const char* key) {
    if (!body.isMember(key) || body[key].isNull()) return std::nullopt;
    if (body[key].isNumeric()) return body[key].asDouble();
    if (body[key].isString()) {
        const auto text = body[key].asString();
        if (text.empty()) return std::nullopt;
        try {
            return std::stod(text);
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<int> read_int(const Json::Value& body, const char* key) {
    const auto value = read_number(body, key);
    if (!value.has_value()) return std::nullopt;
    return static_cast<int>(*value);
}

report::BridgeProfileInput parse_input(const Json::Value& body) {
    report::BridgeProfileInput input;
    input.business_code = read_text(body, "business_code");
    input.route_number = read_text(body, "route_number");
    input.route_name = read_text(body, "route_name");
    input.administrative_region = read_text(body, "administrative_region");
    input.station_mark = read_text(body, "station_mark");
    input.longitude = read_number(body, "longitude");
    input.latitude = read_number(body, "latitude");

    input.bridge_type = read_text(body, "bridge_type");
    input.bridge_scale = read_text(body, "bridge_scale");
    input.span_combination = read_text(body, "span_combination");
    input.bridge_length_m = read_number(body, "bridge_length_m");
    input.bridge_width_m = read_number(body, "bridge_width_m");
    input.built_year = read_int(body, "built_year");

    input.skew_angle_deg = read_number(body, "skew_angle_deg");
    input.carriageway_width_m = read_number(body, "carriageway_width_m");
    input.sidewalk_width_m = read_number(body, "sidewalk_width_m");

    input.deck_pavement = read_text(body, "deck_pavement");
    input.expansion_joint_type = read_text(body, "expansion_joint_type");
    input.expansion_joint_piers = read_text(body, "expansion_joint_piers");
    input.bearing_type = read_text(body, "bearing_type");

    input.superstructure_form = read_text(body, "superstructure_form");
    input.girders_per_span = read_int(body, "girders_per_span");
    input.girder_height_m = read_number(body, "girder_height_m");

    input.abutment_form = read_text(body, "abutment_form");
    input.pier_form = read_text(body, "pier_form");
    input.foundation_form = read_text(body, "foundation_form");

    input.design_load = read_text(body, "design_load");
    input.design_org = read_text(body, "design_org");
    input.construction_org = read_text(body, "construction_org");
    input.maintenance_org = read_text(body, "maintenance_org");
    input.supervision_org = read_text(body, "supervision_org");
    return input;
}

}  // namespace

void register_bridge_profile_routes(const drogon::orm::DbClientPtr& db_client) {
    const std::string profile_path = "/api/bridges/{bridge_id}/profile";
    register_options_handler(profile_path);

    drogon::app().registerHandler(
        profile_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& bridge_id) {
            if (!is_valid_uuid(bridge_id)) {
                respond_bridge_not_found(callback);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::BridgeProfileRepository repository(db_client);
                const auto profile = repository.find(bridge_id);
                if (!profile.has_value()) {
                    respond_bridge_not_found(callback);
                    return;
                }
                respond_json(callback, profile->to_json());
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    // 整体覆盖，不做增量合并：编辑界面一次提交一整张档案表，把某一项清空是正当
    // 操作（原来录错了），增量合并会让「清空」无法表达。
    drogon::app().registerHandler(
        profile_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& bridge_id) {
            if (!is_valid_uuid(bridge_id)) {
                respond_bridge_not_found(callback);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                if (!user->is_admin()) {
                    respond_forbidden(callback);
                    return;
                }
                const auto body = request->getJsonObject();
                if (body == nullptr) {
                    respond_json(callback,
                        make_error_body("bridge_profile_request_invalid", "请求体不是 JSON。"),
                        drogon::k400BadRequest);
                    return;
                }
                db::BridgeProfileRepository repository(db_client);
                switch (repository.save(bridge_id, parse_input(*body))) {
                    case report::BridgeProfileWriteStatus::BridgeNotFound:
                        respond_bridge_not_found(callback);
                        return;
                    case report::BridgeProfileWriteStatus::MeasureOutOfRange:
                        respond_json(callback,
                            make_error_body("bridge_profile_measure_out_of_range",
                                "梁片数、梁高和宽度必须大于 0，斜交角要在 0 到 180 度之间，"
                                "经度在 ±180、纬度在 ±90 之内。"),
                            drogon::k400BadRequest);
                        return;
                    case report::BridgeProfileWriteStatus::Ok:
                        break;
                }
                const auto saved = repository.find(bridge_id);
                respond_json(callback, saved.has_value() ? saved->to_json()
                                                         : Json::Value(Json::objectValue));
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Put});
}

}  // namespace bridge_report::http
