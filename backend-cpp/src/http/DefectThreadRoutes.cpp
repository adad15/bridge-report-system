#include "bridge_report/http/DefectThreadRoutes.hpp"

#include <string>
#include <utility>

#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>
#include <json/json.h>

#include "bridge_report/db/DefectThreadRepository.hpp"
#include "bridge_report/db/ThreadResolutionRepository.hpp"
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

/// already_completed 是**成功**，不是冲突——它表示"你要的状态已经达成"。
drogon::HttpStatusCode triage_apply_status(const db::TriageApplyOutcome& outcome) {
    switch (outcome.status) {
        case db::TriageApplyStatus::Applied:
        case db::TriageApplyStatus::AlreadyCompleted:
            return drogon::k200OK;
        case db::TriageApplyStatus::BatchChanged:
            return drogon::k409Conflict;
        case db::TriageApplyStatus::Rejected:
            break;
    }
    for (const auto& issue : outcome.issues) {
        if (issue.code == "db_write_failed" || issue.code == "database_commit_failed") {
            return drogon::k500InternalServerError;
        }
    }
    return drogon::k409Conflict;
}

Json::Value triage_apply_body(const db::TriageApplyOutcome& outcome) {
    Json::Value body;
    if (outcome.status == db::TriageApplyStatus::Applied
        || outcome.status == db::TriageApplyStatus::AlreadyCompleted) {
        body["status"] = outcome.status == db::TriageApplyStatus::Applied
            ? "applied" : "already_completed";
        body["groups_applied"] = static_cast<int>(outcome.results.size());
        body["threads_created"] = outcome.threads_created;
        body["observations_bound"] = outcome.observations_bound;
        body["results"] = Json::Value(Json::arrayValue);
        for (const auto& result : outcome.results) {
            Json::Value item;
            item["group_id"] = result.group_id;
            item["bridge_component_id"] = result.bridge_component_id;
            item["thread_id"] = result.thread_id;
            item["thread_system_number"] = result.thread_system_number;
            item["outcome"] = result.outcome;
            body["results"].append(item);
        }
        return body;
    }

    body["code"] = "thread_triage_conflict";
    body["message"] = "批次中的部分观测已经变化，请处理后重试。";
    // 整批失败时列出**全部**问题，不止第一个——否则用户要反复提交才能看完。
    body["issues"] = Json::Value(Json::arrayValue);
    for (const auto& issue : outcome.issues) {
        Json::Value item;
        item["reason_code"] = issue.code;
        item["message"] = issue.message;
        item["group_id"] = issue.group_id.empty()
            ? Json::Value(Json::nullValue) : Json::Value(issue.group_id);
        item["bridge_component_id"] = issue.bridge_component_id.empty()
            ? Json::Value(Json::nullValue) : Json::Value(issue.bridge_component_id);
        item["observation_id"] = issue.observation_id.empty()
            ? Json::Value(Json::nullValue) : Json::Value(issue.observation_id);
        body["issues"].append(item);
    }
    return body;
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

std::optional<std::string> parse_triage_apply_request(
    const Json::Value& body, TriageApplyRequestBody& out) {
    if (!body.isObject()) return "invalid_json_body";

    const auto batch_id = non_empty_string_member(body, "batch_id");
    const auto action = non_empty_string_member(body, "action");
    if (!batch_id.has_value() || !action.has_value()) return "triage_required_field_missing";
    if (*action != "create" && *action != "bind") return "triage_invalid_action";

    if (!body["groups"].isArray() || body["groups"].empty()) return "empty_group_selection";
    if (static_cast<int>(body["groups"].size()) > kTriageApplyMaxGroups) {
        return "batch_too_large";
    }

    out.batch_id = *batch_id;
    out.action = *action;
    out.batch_fingerprint = non_empty_string_member(body, "batch_fingerprint").value_or("");
    out.idempotency_key = non_empty_string_member(body, "idempotency_key").value_or("");

    for (const auto& item : body["groups"]) {
        if (!item.isObject()) return "triage_required_field_missing";
        TriageApplyRequestBody::Group group;
        const auto group_id = non_empty_string_member(item, "group_id");
        if (!group_id.has_value()) return "triage_required_field_missing";
        group.group_id = *group_id;
        group.target_thread_id = non_empty_string_member(item, "target_thread_id").value_or("");

        // create 带目标、bind 缺目标都是请求本身不合法，不必进事务就能判。
        if (*action == "create" && !group.target_thread_id.empty()) {
            return "unexpected_target_thread";
        }
        if (*action == "bind" && group.target_thread_id.empty()) {
            return "bind_target_required";
        }
        if (!group.target_thread_id.empty() && !is_valid_uuid(group.target_thread_id)) {
            return "triage_required_field_missing";
        }

        if (!item["observations"].isArray() || item["observations"].empty()) {
            return "empty_group_selection";
        }
        for (const auto& entry : item["observations"]) {
            const auto id = non_empty_string_member(entry, "id");
            const auto token = non_empty_string_member(entry, "updated_at");
            if (!id.has_value() || !token.has_value() || !is_valid_uuid(*id)) {
                return "triage_required_field_missing";
            }
            group.observations.push_back(
                TriageApplyRequestBody::Group::Observation{*id, *token});
        }
        out.groups.push_back(std::move(group));
    }
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

    register_options_handler("/api/bridges/{bridge_id}/thread-triage/apply");
    drogon::app().registerHandler(
        "/api/bridges/{bridge_id}/thread-triage/apply",
        [db_client](
            const drogon::HttpRequestPtr& request, HttpCallback&& callback,
            const std::string& bridge_id) {
            if (!is_valid_uuid(bridge_id)) {
                respond_json(callback, make_error_body("bridge_not_found", "指定的桥梁不存在"),
                             drogon::k404NotFound);
                return;
            }
            const auto body = request->getJsonObject();
            TriageApplyRequestBody parsed;
            if (body == nullptr) {
                respond_json(callback, make_error_body("invalid_json_body", "请求体不是合法 JSON。"),
                             drogon::k400BadRequest);
                return;
            }
            if (const auto error = parse_triage_apply_request(*body, parsed)) {
                respond_json(callback, make_error_body(*error, "批量应用请求不合法。"),
                             drogon::k400BadRequest);
                return;
            }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback);
                    return;
                }
                db::TriageApplyRequest apply_request;
                apply_request.bridge_id = bridge_id;
                apply_request.batch_id = parsed.batch_id;
                apply_request.batch_fingerprint = parsed.batch_fingerprint;
                apply_request.idempotency_key = parsed.idempotency_key;
                apply_request.action = parsed.action == "bind"
                    ? review::TriageAction::Bind : review::TriageAction::Create;
                for (const auto& group : parsed.groups) {
                    db::TriageApplyGroup apply_group;
                    apply_group.group_id = group.group_id;
                    apply_group.target_thread_id = group.target_thread_id;
                    for (const auto& observation : group.observations) {
                        apply_group.observations.push_back(
                            db::TriageApplyObservation{observation.id, observation.updated_at});
                    }
                    apply_request.groups.push_back(std::move(apply_group));
                }

                const auto outcome =
                    db::ThreadResolutionRepository(db_client).apply(apply_request);
                respond_json(callback, triage_apply_body(outcome), triage_apply_status(outcome));
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );
}

}  // namespace bridge_report::http
