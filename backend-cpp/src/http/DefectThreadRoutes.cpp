#include "bridge_report/http/DefectThreadRoutes.hpp"

#include <string>
#include <utility>

#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>
#include <json/json.h>

#include "bridge_report/db/DefectThreadRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {

namespace {

std::optional<std::string> non_empty_string_member(const Json::Value& body, const char* key) {
    if (!body.isMember(key) || !body[key].isString() || body[key].asString().empty()) {
        return std::nullopt;
    }
    return body[key].asString();
}

drogon::HttpStatusCode status_for_error(const std::string& code) {
    if (code == "defect_observation_not_found" || code == "defect_thread_not_found") {
        return drogon::k404NotFound;
    }
    if (code == "thread_required_field_missing" || code == "invalid_json_body") {
        return drogon::k400BadRequest;
    }
    if (code == "db_write_failed" || code == "database_commit_failed") {
        return drogon::k500InternalServerError;
    }
    return drogon::k409Conflict;
}

void respond_outcome(const HttpCallback& callback, const db::ThreadBindingOutcome& outcome) {
    if (outcome.success) {
        respond_json(callback, outcome.body);
        return;
    }
    respond_json(callback, make_error_body(outcome.error_code, outcome.error_message),
                 status_for_error(outcome.error_code));
}

}  // namespace

std::optional<std::string> parse_create_thread_request(const Json::Value& body, CreateThreadRequest& out) {
    if (!body.isObject()) {
        return "invalid_json_body";
    }
    const auto component_id = non_empty_string_member(body, "bridge_component_id");
    const auto observation_id = non_empty_string_member(body, "first_observation_id");
    const auto token = non_empty_string_member(body, "expected_observation_updated_at");
    if (!component_id.has_value() || !observation_id.has_value() || !token.has_value()
        || !is_valid_uuid(*component_id) || !is_valid_uuid(*observation_id)) {
        return "thread_required_field_missing";
    }
    // 线索必须携带标准病害类型与标准详细位置（模块 06 规格 §11.2）。
    const auto defect_type = non_empty_string_member(body, "defect_type");
    const auto defect_location = non_empty_string_member(body, "defect_location");
    if (!defect_type.has_value() || !defect_location.has_value()) {
        return "thread_required_field_missing";
    }
    out.bridge_component_id = *component_id;
    out.defect_type = *defect_type;
    out.defect_location = *defect_location;
    out.first_observation_id = *observation_id;
    out.expected_observation_updated_at = *token;
    out.thread_name = non_empty_string_member(body, "thread_name");
    return std::nullopt;
}

std::optional<std::string> parse_bind_observation_request(const Json::Value& body, BindObservationRequest& out) {
    if (!body.isObject()) {
        return "invalid_json_body";
    }
    const auto token = non_empty_string_member(body, "expected_observation_updated_at");
    if (!token.has_value()) {
        return "thread_required_field_missing";
    }
    if (body.isMember("defect_thread_id") && !body["defect_thread_id"].isNull()) {
        if (!body["defect_thread_id"].isString() || !is_valid_uuid(body["defect_thread_id"].asString())) {
            return "thread_required_field_missing";
        }
        out.defect_thread_id = body["defect_thread_id"].asString();
    } else {
        out.defect_thread_id = std::nullopt;
    }
    out.expected_observation_updated_at = *token;
    out.confirm_rebind = body.isMember("confirm_rebind") && body["confirm_rebind"].isBool()
        && body["confirm_rebind"].asBool();
    return std::nullopt;
}

void register_defect_thread_routes(const drogon::orm::DbClientPtr& db_client) {
    register_options_handler("/api/defect-threads");
    register_options_handler("/api/defect-observations/{observation_id}/defect-thread");

    drogon::app().registerHandler(
        "/api/defect-threads",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            const auto body = request->getJsonObject();
            if (body == nullptr) {
                respond_json(callback, make_error_body("invalid_json_body", "请求体不是合法的 JSON。"),
                             drogon::k400BadRequest);
                return;
            }
            CreateThreadRequest parsed;
            if (const auto error = parse_create_thread_request(*body, parsed)) {
                respond_json(
                    callback,
                    make_error_body(*error, "创建线索必须提供构件、标准病害类型、标准详细位置、首条观测与并发令牌。"),
                    status_for_error(*error));
                return;
            }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback);
                    return;
                }

                db::DefectThreadRepository repository(db_client);
                respond_outcome(callback, repository.create_thread(
                    parsed.bridge_component_id, parsed.defect_type, parsed.defect_location,
                    parsed.first_observation_id, parsed.expected_observation_updated_at, parsed.thread_name));
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );

    drogon::app().registerHandler(
        "/api/defect-observations/{observation_id}/defect-thread",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& observation_id) {
            if (!is_valid_uuid(observation_id)) {
                respond_json(callback, make_error_body("defect_observation_not_found", "指定的病害观测不存在"),
                             drogon::k404NotFound);
                return;
            }
            const auto body = request->getJsonObject();
            if (body == nullptr) {
                respond_json(callback, make_error_body("invalid_json_body", "请求体不是合法的 JSON。"),
                             drogon::k400BadRequest);
                return;
            }
            BindObservationRequest parsed;
            if (const auto error = parse_bind_observation_request(*body, parsed)) {
                respond_json(callback,
                             make_error_body(*error, "绑定请求必须携带并发令牌；目标线索 ID 须为合法 UUID 或 null。"),
                             status_for_error(*error));
                return;
            }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback);
                    return;
                }

                db::DefectThreadRepository repository(db_client);
                respond_outcome(callback, repository.bind_observation(
                    observation_id, parsed.defect_thread_id, parsed.expected_observation_updated_at,
                    parsed.confirm_rebind));
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Put}
    );
}

}  // namespace bridge_report::http
