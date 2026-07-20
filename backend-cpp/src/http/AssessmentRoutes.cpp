#include "bridge_report/http/AssessmentRoutes.hpp"

#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/Cors.hpp"
#include "bridge_report/http/EditLockRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {

bool parse_assessment_preview_request(
    const Json::Value& body,
    assessment::AssessmentPreviewPayload& output) {
    if (!body.isObject() || !body["draft"].isObject() ||
        !body["client_revision"].isInt() || body["client_revision"].asInt() < 0) {
        return false;
    }
    output.draft = body["draft"];
    output.client_revision = body["client_revision"].asInt();
    return true;
}

void register_assessment_routes(
    const drogon::orm::DbClientPtr& db_client,
    std::shared_ptr<const standards::StandardRegistry> registry) {
    const std::string path = "/api/import-records/{import_record_id}/assessment-preview";
    register_options_handler(path);
    drogon::app().registerHandler(
        path,
        [db_client, registry = std::move(registry)](
            const drogon::HttpRequestPtr& request,
            HttpCallback&& callback,
            const std::string& import_record_id) {
            if (!is_valid_uuid(import_record_id)) {
                respond_import_record_not_found(callback);
                return;
            }
            const auto body = request->getJsonObject();
            assessment::AssessmentPreviewPayload payload;
            if (body == nullptr || !parse_assessment_preview_request(*body, payload)) {
                respond_json(callback, make_error_body(
                    "assessment_request_invalid", "请求必须包含对象 draft 和非负整数 client_revision。"),
                    drogon::k400BadRequest);
                return;
            }
            try {
                const auto user = authenticate_request(db_client, request);
                if (!user.has_value()) {
                    respond_unauthorized(callback);
                    return;
                }
                assessment::AssessmentService service(db_client, registry);
                const auto outcome = service.preview(
                    import_record_id, payload, *user, edit_lock_token_from_request(request));
                if (outcome.status == assessment::AssessmentServiceStatus::NotFound) {
                    respond_import_record_not_found(callback);
                    return;
                }
                const auto status = outcome.status == assessment::AssessmentServiceStatus::LockRejected
                    ? drogon::k409Conflict
                    : drogon::k200OK;
                respond_json(callback, outcome.preview.to_json(), status);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (const std::exception&) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});
}

}  // namespace bridge_report::http
