#include "bridge_report/http/InspectionYearDeletionRoutes.hpp"

#include <algorithm>
#include <cctype>

#include <drogon/orm/Exception.h>

#include "bridge_report/db/InspectionYearDeletionRepository.hpp"
#include "bridge_report/deletion/ArchiveFileCleanupCoordinator.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {
namespace {

std::string trim_copy(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

void respond_year_not_found(const HttpCallback& callback) {
    respond_json(callback, make_error_body("inspection_year_not_found", "指定的年度检测不存在。"),
                 drogon::k404NotFound);
}

}  // namespace

std::optional<std::string> parse_delete_inspection_year_request(
    const Json::Value& body,
    DeleteInspectionYearRequest& out
) {
    if (!body.isObject()) return "invalid_json_body";
    if (!body["impact_token"].isString() || trim_copy(body["impact_token"].asString()).empty())
        return "deletion_impact_token_required";
    if (!body["confirmation_text"].isString() || trim_copy(body["confirmation_text"].asString()).empty())
        return "deletion_confirmation_required";
    if (!body["reason"].isString()) return "deletion_reason_required";
    out.impact_token = trim_copy(body["impact_token"].asString());
    out.confirmation_text = body["confirmation_text"].asString();
    out.reason = trim_copy(body["reason"].asString());
    if (out.reason.empty()) return "deletion_reason_required";
    if (out.reason.size() > 4000) return "deletion_reason_too_long";
    return std::nullopt;
}

void register_inspection_year_deletion_routes(
    const drogon::orm::DbClientPtr& db_client,
    const std::shared_ptr<deletion::ArchiveFileCleanupCoordinator>& cleanup_coordinator
) {
    register_options_handler("/api/inspection-years/{inspection_year_id}/deletion-impact");
    register_options_handler("/api/inspection-years/{inspection_year_id}");

    drogon::app().registerHandler(
        "/api/inspection-years/{inspection_year_id}/deletion-impact",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback, const std::string& id) {
            if (!is_valid_uuid(id)) { respond_year_not_found(callback); return; }
            try {
                const auto user = authenticate_request(db_client, request);
                if (!user.has_value()) { respond_unauthorized(callback); return; }
                if (!user->is_admin()) { respond_forbidden(callback); return; }
                db::InspectionYearDeletionRepository repository(db_client);
                const auto plan = repository.preview(id);
                if (!plan.has_value()) { respond_year_not_found(callback); return; }
                respond_json(callback, plan->to_public_json());
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/api/inspection-years/{inspection_year_id}",
        [db_client, cleanup_coordinator](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                                  const std::string& id) {
            if (!is_valid_uuid(id)) { respond_year_not_found(callback); return; }
            const auto body = request->getJsonObject();
            if (body == nullptr) {
                respond_json(callback, make_error_body("invalid_json_body", "请求体不是合法的 JSON。"),
                             drogon::k400BadRequest);
                return;
            }
            DeleteInspectionYearRequest parsed;
            if (const auto error = parse_delete_inspection_year_request(*body, parsed)) {
                respond_json(callback, make_error_body(*error, "请填写删除原因、确认文字并使用最新影响预览。"),
                             drogon::k400BadRequest);
                return;
            }
            try {
                const auto user = authenticate_request(db_client, request);
                if (!user.has_value()) { respond_unauthorized(callback); return; }
                if (!user->is_admin()) { respond_forbidden(callback); return; }

                db::InspectionYearDeletionRepository repository(db_client);
                const deletion::DeletionActorSnapshot actor{user->id, user->username, user->display_name};
                const auto outcome = repository.delete_year(
                    id, parsed.impact_token, parsed.confirmation_text, parsed.reason, actor);
                switch (outcome.status) {
                    case deletion::DeleteInspectionYearStatus::NotFound:
                        respond_year_not_found(callback);
                        return;
                    case deletion::DeleteInspectionYearStatus::Locked: {
                        auto error = make_error_body("inspection_year_edit_locked",
                            "该年度仍有导入记录正在编辑，请等待编辑者退出或编辑锁过期后重试。");
                        if (outcome.current_plan.has_value())
                            error["active_edit_locks"] = outcome.current_plan->to_public_json()["active_edit_locks"];
                        respond_json(callback, error, drogon::k409Conflict);
                        return;
                    }
                    case deletion::DeleteInspectionYearStatus::ImpactChanged: {
                        auto error = make_error_body("deletion_impact_changed",
                            "删除影响范围已经变化，请重新查看警告内容并再次确认。");
                        if (outcome.current_plan.has_value()) error["current_impact"] = outcome.current_plan->to_public_json();
                        respond_json(callback, error, drogon::k409Conflict);
                        return;
                    }
                    case deletion::DeleteInspectionYearStatus::FormalAssessmentPresent: {
                        auto error = make_error_body("inspection_year_formal_assessment_present",
                            "该年度存在已完成的正式评定，不能删除。正式评定是不可变的业务记录，"
                            "如确需删除请先处理该评定。");
                        if (outcome.current_plan.has_value())
                            error["current_impact"] = outcome.current_plan->to_public_json();
                        respond_json(callback, error, drogon::k409Conflict);
                        return;
                    }
                    case deletion::DeleteInspectionYearStatus::Failed:
                        respond_db_unavailable(callback);
                        return;
                    case deletion::DeleteInspectionYearStatus::Deleted:
                        break;
                }

                deletion::FileCleanupSummary cleanup;
                try {
                    cleanup = cleanup_coordinator->process_annual_audit(*outcome.deletion_audit_id);
                } catch (const std::exception&) {
                    cleanup.failed = outcome.current_plan.has_value()
                        ? outcome.current_plan->counts.archived_files_to_delete : 1;
                }
                Json::Value response;
                response["deleted"] = true;
                response["deletion_audit_id"] = *outcome.deletion_audit_id;
                response["bridge_id"] = outcome.current_plan->bridge_id;
                response["inspection_year"] = outcome.current_plan->inspection_year;
                response["deleted_counts"] = outcome.current_plan->counts.to_json();
                response["next_inspection_year_id"] = outcome.next_inspection_year_id.has_value()
                    ? Json::Value(*outcome.next_inspection_year_id) : Json::Value(Json::nullValue);
                response["file_cleanup"]["completed"] = cleanup.completed;
                response["file_cleanup"]["failed"] = cleanup.failed;
                response["file_cleanup"]["pending"] = cleanup_coordinator->pending_annual_items(
                    *outcome.deletion_audit_id);
                respond_json(callback, response);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Delete}
    );
}

}  // namespace bridge_report::http
