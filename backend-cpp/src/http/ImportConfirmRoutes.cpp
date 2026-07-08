#include "bridge_report/http/ImportConfirmRoutes.hpp"

#include <sstream>
#include <string>

#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>
#include <json/json.h>

#include "bridge_report/db/ReviewRepository.hpp"
#include "bridge_report/http/RouteHelpers.hpp"
#include "bridge_report/review/ConfirmPlan.hpp"
#include "bridge_report/review/PreflightReport.hpp"
#include "bridge_report/review/ReviewModels.hpp"

namespace bridge_report::http {

namespace {

void respond_import_record_not_found(const HttpCallback& callback) {
    respond_json(
        callback,
        make_error_body("import_record_not_found", "指定的导入记录不存在"),
        drogon::k404NotFound
    );
}

// parsed_result_json 存储为 jsonb 文本；解析失败（理论上不应发生，防御式处理）时退化为空对象，
// 与 ReviewRoutes.cpp 中的同名辅助函数逻辑一致（未共享——仅两处各自一份的小函数，见模块 05
// Task 8 实施计划"Keep the move mechanical"）。
Json::Value parse_parsed_result_json(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        return Json::Value(Json::objectValue);
    }
    return root;
}

void register_options_handler(const std::string& path) {
    drogon::app().registerHandler(
        path,
        [](const drogon::HttpRequestPtr&, HttpCallback&& callback) {
            auto response = drogon::HttpResponse::newHttpResponse();
            apply_local_dev_cors_headers(response);
            callback(response);
        },
        {drogon::Options, "drogon::HttpOptionsMiddleware"}
    );
}

// POST /api/import-records/{import_record_id}/preflight-confirm：入库前检查。
// 无请求体，只读——不修改导入记录状态，只是把当前 parsed_result_json 跑一遍
// build_preflight_report 并把报告原样返回，供前端在真正确认入库前展示阻断项/警告。
void register_preflight_confirm_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/import-records/{import_record_id}/preflight-confirm",
        [db_client](
            const drogon::HttpRequestPtr&,
            HttpCallback&& callback,
            const std::string& import_record_id
        ) {
            if (!is_valid_uuid(import_record_id)) {
                respond_import_record_not_found(callback);
                return;
            }

            try {
                db::ReviewRepository repository(db_client);
                const auto detail = repository.get_import_record_detail(import_record_id);
                if (!detail.has_value()) {
                    respond_import_record_not_found(callback);
                    return;
                }

                const auto parsed_result = parse_parsed_result_json(detail->parsed_result_json);
                const auto effective_year = review::resolve_effective_inspection_year(*detail, parsed_result);
                const bool has_current_annual_facts = effective_year.has_value()
                    && repository.has_current_annual_facts(detail->bridge_id, *effective_year);

                const auto context =
                    review::build_preflight_context(*detail, effective_year, has_current_annual_facts);
                const auto report = review::build_preflight_report(parsed_result, context);

                respond_json(callback, report.to_json());
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );
}

// 请求体字段读取：均带默认值，缺请求体/缺字段/字段类型不对都静默退化为默认值，
// 不额外报错——只有整个请求体不是合法 JSON 才算错误（见 respond_invalid_json_body 的调用点）。
bool confirm_revision_from_body(const Json::Value* body) {
    if (body == nullptr || !body->isObject() || !body->isMember("confirm_revision")
        || !(*body)["confirm_revision"].isBool()) {
        return false;
    }
    return (*body)["confirm_revision"].asBool();
}

std::string confirmation_note_from_body(const Json::Value* body) {
    if (body == nullptr || !body->isObject() || !body->isMember("confirmation_note")
        || !(*body)["confirmation_note"].isString()) {
        return std::string();
    }
    return (*body)["confirmation_note"].asString();
}

Json::Value written_counts_to_json(const db::ConfirmWrittenCounts& written) {
    Json::Value json;
    json["defect_observations"] = written.defect_observations;
    json["defect_measurements"] = written.defect_measurements;
    json["defect_photos"] = written.defect_photos;
    json["condition_ratings"] = written.condition_ratings;
    return json;
}

// 仓储层结果到 HTTP 响应的映射：wrong_status / revision_confirmation_required 均为业务拒绝
// （事务已回滚，导入记录仍是待校对）→ 409；db_write_failed 是写入阶段的数据库异常 → 500。
void respond_confirm_outcome_failure(const HttpCallback& callback, const db::ConfirmOutcome& outcome) {
    if (outcome.error_code == "import_record_wrong_status") {
        respond_json(
            callback,
            make_error_body(outcome.error_code, "导入记录状态已变化，无法入库，请刷新后重试。"),
            drogon::k409Conflict
        );
        return;
    }
    if (outcome.error_code == "revision_confirmation_required") {
        respond_json(
            callback,
            make_error_body(outcome.error_code, "同桥同年已有当前有效事实，需显式确认修订版。"),
            drogon::k409Conflict
        );
        return;
    }
    respond_json(
        callback,
        make_error_body("db_write_failed", outcome.error_message.empty() ? "入库写入失败。" : outcome.error_message),
        drogon::k500InternalServerError
    );
}

// POST /api/import-records/{import_record_id}/confirm：正式入库——唯一会写入
// defect_observations / defect_measurements / defect_photos / condition_ratings /
// bridge_components / inspection_years 等正式事实表的路径。
void register_confirm_route(const drogon::orm::DbClientPtr& db_client) {
    drogon::app().registerHandler(
        "/api/import-records/{import_record_id}/confirm",
        [db_client](
            const drogon::HttpRequestPtr& request,
            HttpCallback&& callback,
            const std::string& import_record_id
        ) {
            if (!is_valid_uuid(import_record_id)) {
                respond_import_record_not_found(callback);
                return;
            }

            // getJsonObject() 在“没有请求体/Content-Type 不是 application/json”与
            // “请求体存在但不是合法 JSON”两种情况下都返回空指针；用 getJsonError() 是否
            // 非空区分二者——前者按规格默认 confirm_revision=false、confirmation_note=""，
            // 只有后者才是 400 invalid_json_body。
            const auto& body_json = request->getJsonObject();
            if (body_json == nullptr && !request->getJsonError().empty()) {
                respond_json(
                    callback,
                    make_error_body("invalid_json_body", "请求体不是合法的 JSON。"),
                    drogon::k400BadRequest
                );
                return;
            }

            const bool confirm_revision = confirm_revision_from_body(body_json.get());
            const std::string confirmation_note = confirmation_note_from_body(body_json.get());

            try {
                db::ReviewRepository repository(db_client);
                const auto detail = repository.get_import_record_detail(import_record_id);
                if (!detail.has_value()) {
                    respond_import_record_not_found(callback);
                    return;
                }

                const auto parsed_result = parse_parsed_result_json(detail->parsed_result_json);
                const auto effective_year = review::resolve_effective_inspection_year(*detail, parsed_result);
                const bool has_current_annual_facts = effective_year.has_value()
                    && repository.has_current_annual_facts(detail->bridge_id, *effective_year);

                const auto context =
                    review::build_preflight_context(*detail, effective_year, has_current_annual_facts);
                const auto preflight = review::build_preflight_report(parsed_result, context);

                if (!preflight.can_confirm) {
                    respond_json(callback, preflight.to_json(), drogon::k409Conflict);
                    return;
                }

                if (preflight.requires_revision_confirmation && !confirm_revision) {
                    respond_json(
                        callback,
                        make_error_body(
                            "revision_confirmation_required", "同桥同年已有当前有效事实，需显式确认修订版。"
                        ),
                        drogon::k409Conflict
                    );
                    return;
                }

                if (!effective_year.has_value()) {
                    // 理论上不会发生：preflight can_confirm=true 已隐含契约校验通过，
                    // resolve_effective_inspection_year 至少能从 inspection.inspection_year 退化出年度。
                    // 防御式处理，提示调用方重新执行入库前检查而不是让空 optional 往下传。
                    respond_json(
                        callback,
                        make_error_body(
                            "effective_inspection_year_unresolved", "无法解析有效检测年度，请重新执行入库前检查。"
                        ),
                        drogon::k409Conflict
                    );
                    return;
                }

                const auto plan = review::build_confirm_plan(parsed_result);
                const auto outcome = repository.confirm_annual_facts(
                    import_record_id, plan, *effective_year, confirm_revision, confirmation_note
                );

                if (!outcome.success) {
                    respond_confirm_outcome_failure(callback, outcome);
                    return;
                }

                Json::Value response_body;
                response_body["confirmed"] = true;
                response_body["inspection_year_id"] = outcome.inspection_year_id;
                response_body["version_number"] = outcome.version_number;
                response_body["written"] = written_counts_to_json(outcome.written);
                respond_json(callback, response_body);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post}
    );
}

}  // 匿名命名空间

void register_import_confirm_routes(const drogon::orm::DbClientPtr& db_client) {
    register_options_handler("/api/import-records/{import_record_id}/preflight-confirm");
    register_options_handler("/api/import-records/{import_record_id}/confirm");

    register_preflight_confirm_route(db_client);
    register_confirm_route(db_client);
}

}  // 命名空间 bridge_report::http
