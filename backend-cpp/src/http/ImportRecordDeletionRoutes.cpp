#include "bridge_report/http/ImportRecordDeletionRoutes.hpp"

#include <algorithm>
#include <cctype>

#include "bridge_report/db/ImportRecordDeletionRepository.hpp"
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

void respond_import_not_found(const HttpCallback& callback) {
    respond_json(callback, make_error_body("import_record_not_found", "指定的导入记录不存在。"),
                 drogon::k404NotFound);
}

}  // namespace

std::optional<std::string> parse_delete_import_record_request(
    const Json::Value& body,
    DeleteImportRecordRequest& out
) {
    if (!body.isObject()) return "invalid_json_body";
    if (!body["impact_token"].isString() || trim_copy(body["impact_token"].asString()).empty())
        return "deletion_impact_token_required";
    if (!body["confirmation_text"].isString() || body["confirmation_text"].asString().empty())
        return "deletion_confirmation_required";
    if (!body["reason"].isString()) return "deletion_reason_required";
    out.impact_token = trim_copy(body["impact_token"].asString());
    out.confirmation_text = body["confirmation_text"].asString();
    out.reason = trim_copy(body["reason"].asString());
    if (out.reason.empty()) return "deletion_reason_required";
    if (out.reason.size() > 1000) return "deletion_reason_too_long";
    return std::nullopt;
}

void register_import_record_deletion_routes(
    const drogon::orm::DbClientPtr& db_client,
    const std::shared_ptr<deletion::ArchiveFileCleanupCoordinator>& cleanup_coordinator
) {
    register_options_handler("/api/import-records/{import_record_id}/deletion-impact");
    register_options_handler("/api/import-records/{import_record_id}");

    drogon::app().registerHandler(
        "/api/import-records/{import_record_id}/deletion-impact",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback, const std::string& id) {
            if (!is_valid_uuid(id)) { respond_import_not_found(callback); return; }
            try {
                const auto user = authenticate_request(db_client, request);
                if (!user.has_value()) { respond_unauthorized(callback); return; }
                if (!user->is_admin()) { respond_forbidden(callback); return; }
                db::ImportRecordDeletionRepository repository(db_client);
                const auto plan = repository.preview(id);
                if (!plan.has_value()) { respond_import_not_found(callback); return; }
                respond_json(callback, plan->to_public_json());
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/api/import-records/{import_record_id}",
        [db_client, cleanup_coordinator](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                                         const std::string& id) {
            if (!is_valid_uuid(id)) { respond_import_not_found(callback); return; }
            const auto body = request->getJsonObject();
            if (!body) {
                respond_json(callback, make_error_body("invalid_json_body", "请求体不是合法的 JSON。"),
                             drogon::k400BadRequest);
                return;
            }
            DeleteImportRecordRequest parsed;
            if (const auto error = parse_delete_import_record_request(*body, parsed)) {
                respond_json(callback, make_error_body(*error, "请填写删除原因、完整确认文字并使用最新影响预览。"),
                             drogon::k400BadRequest);
                return;
            }
            try {
                const auto user = authenticate_request(db_client, request);
                if (!user.has_value()) { respond_unauthorized(callback); return; }
                if (!user->is_admin()) { respond_forbidden(callback); return; }
                db::ImportRecordDeletionRepository repository(db_client);
                const auto preview = repository.preview(id);
                if (!preview.has_value()) { respond_import_not_found(callback); return; }
                if (parsed.confirmation_text != preview->confirmation_text()) {
                    respond_json(callback, make_error_body("deletion_confirmation_incorrect", "确认文字不正确。"),
                                 drogon::k400BadRequest);
                    return;
                }
                const deletion::DeletionActorSnapshot actor{user->id, user->username, user->display_name};
                const auto outcome = repository.delete_import_record(
                    id, parsed.impact_token, parsed.reason, actor);
                switch (outcome.status) {
                    case deletion::DeleteImportRecordStatus::NotFound:
                        respond_import_not_found(callback);
                        return;
                    case deletion::DeleteImportRecordStatus::NotDeletable:
                        respond_json(callback, make_error_body("import_record_not_deletable",
                            "该导入记录已进入正式只读状态，不能单独删除。"), drogon::k409Conflict);
                        return;
                    case deletion::DeleteImportRecordStatus::Locked: {
                        auto error = make_error_body("import_record_edit_locked",
                            "该导入记录正在编辑，请等待编辑者退出或编辑锁过期后重试。");
                        if (outcome.current_plan.has_value())
                            error["active_edit_locks"] = outcome.current_plan->to_public_json()["active_edit_locks"];
                        respond_json(callback, error, drogon::k409Conflict);
                        return;
                    }
                    case deletion::DeleteImportRecordStatus::HasFormalFacts:
                        respond_json(callback, make_error_body("import_record_has_formal_facts",
                            "该导入记录已经形成正式病害、照片或评分事实，不能单独删除。"),
                            drogon::k409Conflict);
                        return;
                    case deletion::DeleteImportRecordStatus::ImpactChanged: {
                        auto error = make_error_body("deletion_impact_changed",
                            "删除影响范围已经变化，请重新查看警告内容并再次确认。");
                        if (outcome.current_plan.has_value())
                            error["current_impact"] = outcome.current_plan->to_public_json();
                        respond_json(callback, error, drogon::k409Conflict);
                        return;
                    }
                    case deletion::DeleteImportRecordStatus::Failed:
                        respond_db_unavailable(callback);
                        return;
                    case deletion::DeleteImportRecordStatus::Deleted:
                        break;
                }

                deletion::FileCleanupSummary cleanup;
                try {
                    cleanup = cleanup_coordinator->process_import_audit(*outcome.deletion_audit_id);
                } catch (...) {
                    cleanup.failed = outcome.current_plan->counts.archived_files_to_delete
                        + outcome.current_plan->counts.temporary_word_files_to_delete
                        + outcome.current_plan->counts.parse_work_directories_to_delete;
                }
                Json::Value response;
                response["deleted"] = true;
                response["deletion_audit_id"] = *outcome.deletion_audit_id;
                response["bridge_id"] = outcome.current_plan->bridge_id;
                response["inspection_year_id"] = outcome.current_plan->inspection_year_id;
                response["deleted_counts"] = outcome.current_plan->counts.to_json();
                response["file_cleanup"]["completed"] = cleanup.completed;
                response["file_cleanup"]["failed"] = cleanup.failed;
                response["file_cleanup"]["pending"] = cleanup_coordinator->pending_import_items(
                    *outcome.deletion_audit_id);
                respond_json(callback, response);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Delete}
    );
}

}  // namespace bridge_report::http
