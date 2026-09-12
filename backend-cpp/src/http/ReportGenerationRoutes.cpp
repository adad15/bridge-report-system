#include "bridge_report/http/ReportGenerationRoutes.hpp"

#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <drogon/orm/Exception.h>

#include "bridge_report/db/ReportGenerationJobRepository.hpp"
#include "bridge_report/db/ReportPreflightRepository.hpp"
#include "bridge_report/db/ReportTemplateRepository.hpp"
#include "bridge_report/db/InspectionReportSettingsRepository.hpp"
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

void respond_year_not_found(const HttpCallback& callback) {
    respond_json(callback, make_error_body("inspection_year_not_found", "年度检查不存在。"),
                 drogon::k404NotFound);
}

void respond_job_not_found(const HttpCallback& callback) {
    respond_json(callback, make_error_body("report_job_not_found", "生成任务不存在。"),
                 drogon::k404NotFound);
}

}  // namespace

void register_report_generation_routes(
    const drogon::orm::DbClientPtr& db_client,
    std::shared_ptr<report::ReportGenerationRunner> runner) {
    const std::string jobs_path = "/api/inspection-years/{id}/report-jobs";
    const std::string current_job_path = "/api/inspection-years/{id}/report-jobs/current";
    const std::string job_path = "/api/report-jobs/{id}";
    const std::string download_path = "/api/report-jobs/{id}/download";

    for (const auto& path : {jobs_path, current_job_path, job_path, download_path}) {
        register_options_handler(path);
    }

    // 创建任务。立刻返回 queued，真正的生成在 runner 自己的线程里跑（设计 §17.3）。
    drogon::app().registerHandler(
        jobs_path,
        [db_client, runner](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_year_not_found(callback);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;

                // 生成前检查在这里再跑一次：前端展示过一遍，但从展示到点按钮之间
                // 数据可能已经变了，阻断项不能只靠界面拦（设计 §16）。
                db::ReportPreflightRepository preflight(db_client);
                const auto checked = preflight.evaluate(id);
                if (!checked.has_value()) {
                    respond_year_not_found(callback);
                    return;
                }
                if (!checked->can_generate()) {
                    auto body = make_error_body(
                        "report_preflight_blocked", "生成前检查未通过，未创建任务。");
                    body["preflight"] = checked->to_json();
                    respond_json(callback, body, drogon::k409Conflict);
                    return;
                }

                db::InspectionReportSettingsRepository settings_repository(db_client);
                const auto settings = settings_repository.find(id);
                std::optional<std::string> template_id;
                std::optional<std::string> template_checksum;
                if (settings.has_value()) template_id = settings->template_id;
                if (template_id.has_value()) {
                    db::ReportTemplateRepository templates(db_client);
                    const auto item = templates.find(*template_id);
                    if (item.has_value()) template_checksum = item->file_checksum;
                }

                db::ReportGenerationJobRepository jobs(db_client);
                const auto created =
                    jobs.create(id, user->id, template_id, template_checksum);
                if (created.outcome == db::JobCreation::Outcome::YearNotFound) {
                    respond_year_not_found(callback);
                    return;
                }
                if (!created.job.has_value()) {
                    respond_db_unavailable(callback);
                    return;
                }
                if (created.outcome == db::JobCreation::Outcome::Created) {
                    runner->enqueue(created.job->id);
                    respond_json(callback, created.job->to_json(), drogon::k201Created);
                    return;
                }
                // 已有进行中的任务：返回它而不是再起一个（设计 §17.3）。前端据此
                // 直接接着轮询，用户看到的是"还在跑"，不是一个报错。
                respond_json(callback, created.job->to_json(), drogon::k200OK);
            } catch (const drogon::orm::DrogonDbException&) {
                respond_db_unavailable(callback);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    // 当前报告：这个年度最近的那个任务，没有就返回 null。
    //
    // 不列历史。系统不保存报告版本（§17.1），用户要的永远是"现在这份能不能下"；
    // 摆一张历史表只会让人以为那些旧文件还在，而它们到期就删了。
    drogon::app().registerHandler(
        current_job_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_year_not_found(callback);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportGenerationJobRepository jobs(db_client);
                const auto job = jobs.find_current(id);
                Json::Value body;
                body["job"] = job.has_value() ? job->to_json() : Json::Value(Json::nullValue);
                respond_json(callback, body);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        job_path,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_job_not_found(callback);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportGenerationJobRepository jobs(db_client);
                const auto job = jobs.find(id);
                if (!job.has_value()) {
                    respond_job_not_found(callback);
                    return;
                }
                respond_json(callback, job->to_json());
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    // 下载成品。按任务 ID 取，不接受客户端传路径——临时路径不可猜不能当成唯一保护
    // （设计 §22 最后一条）。
    //
    // 待办：§22 还要求按桥梁和年度检查的权限校验。本系统目前只有 admin/normal 两种
    // 角色，没有"某人对某座桥有查看权"这一层，年度配置的读写接口同样只做登录校验。
    // 等权限模型建起来时，这三个接口要跟着补上，别只改这一个。
    drogon::app().registerHandler(
        download_path,
        [db_client, runner](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& id) {
            if (!is_valid_uuid(id)) {
                respond_job_not_found(callback);
                return;
            }
            try {
                const auto user = require_user(db_client, request, callback);
                if (!user.has_value()) return;
                db::ReportGenerationJobRepository jobs(db_client);
                const auto job = jobs.find(id);
                if (!job.has_value()) {
                    respond_job_not_found(callback);
                    return;
                }
                if (!job->can_download()) {
                    // 这一条覆盖三种情况：还在跑、失败了、已经过期。状态本身就是
                    // 答案，前端按 status 提示"重新生成"还是"请稍候"。
                    auto body = make_error_body(
                        "report_job_not_downloadable", "这个任务没有可下载的报告。");
                    body["status"] = report::job_status_text(job->status);
                    respond_json(callback, body, drogon::k409Conflict);
                    return;
                }
                const auto path = runner->output_path(*job);
                if (!path.has_value()) {
                    auto body = make_error_body(
                        "report_job_file_missing", "报告文件已不存在，请重新生成。");
                    body["status"] = report::job_status_text(job->status);
                    respond_json(callback, body, drogon::k409Conflict);
                    return;
                }
                auto response = drogon::HttpResponse::newFileResponse(
                    path->string(), "", drogon::CT_CUSTOM,
                    "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
                    request);
                response->addHeader(
                    "Content-Disposition",
                    attachment_disposition(job->download_filename.value_or("report.docx"),
                                           "report.docx"));
                apply_local_dev_cors_headers(response);
                callback(response);
            } catch (const std::filesystem::filesystem_error&) {
                respond_json(callback,
                    make_error_body("report_job_file_missing", "报告文件无法读取，请重新生成。"),
                    drogon::k409Conflict);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});
}

}  // namespace bridge_report::http
