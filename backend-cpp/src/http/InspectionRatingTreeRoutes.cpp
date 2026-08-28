#include "bridge_report/http/InspectionRatingTreeRoutes.hpp"

#include <string>

#include "bridge_report/db/InspectionRatingTreeRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/EditLockRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {

namespace {

void respond_binding(const HttpCallback& callback, const db::RatingTreeBindingOutcome& outcome) {
    if (outcome.status == db::RatingTreeBindingStatus::Ok) {
        Json::Value body(Json::objectValue);
        body["bound"] = true;
        respond_json(callback, body);
        return;
    }
    if (outcome.status == db::RatingTreeBindingStatus::NotFound) {
        respond_import_record_not_found(callback);
        return;
    }
    if (outcome.status == db::RatingTreeBindingStatus::EditLockInvalid) {
        respond_json(callback, make_error_body("edit_lock_required", "需要编辑权才能绑定评定树。"),
                     drogon::k409Conflict);
        return;
    }
    if (outcome.status == db::RatingTreeBindingStatus::Conflict) {
        respond_json(callback,
                     make_error_body(outcome.error_code.empty()
                                         ? "component_binding_conflict" : outcome.error_code,
                                     outcome.error_message),
                     drogon::k409Conflict);
        return;
    }
    respond_db_unavailable(callback);
}

void respond_rating_tree_binding(
    const HttpCallback& callback,
    const db::RatingTreeBindingOutcome& outcome) {
    if (outcome.status == db::RatingTreeBindingStatus::Conflict) {
        respond_json(callback, make_error_body(
            "rating_tree_binding_conflict",
            "仅可为待校对、尚无成功正式评定且已确认构件台账的年度绑定评定树。"),
            drogon::k409Conflict);
        return;
    }
    respond_binding(callback, outcome);
}

// 取回请求根节点的 expected_inventory_revision_id。缺失、空串或非 UUID 都是入参错误，
// 返回 400；那和"版本确实变了"是两回事，不能让前端把它当成需要刷新概览的冲突。
// 批量绑定同样从根节点取，不逐个 target 重复。
bool parse_expected_revision(const Json::Value* body, std::string& expected,
                             const HttpCallback& callback,
                             const char* error_code = "invalid_component_binding") {
    if (body == nullptr || !(*body)["expected_inventory_revision_id"].isString()
        || !is_valid_uuid((*body)["expected_inventory_revision_id"].asString())) {
        respond_json(callback, make_error_body(
            error_code, "expected_inventory_revision_id 必须是有效的台账版本 UUID。"),
            drogon::k400BadRequest);
        return false;
    }
    expected = (*body)["expected_inventory_revision_id"].asString();
    return true;
}

}  // namespace

RatingTreeBindingErrorResponse rating_tree_binding_error_response(const db::RatingTreeBindingOutcome& outcome) {
    switch (outcome.status) {
        case db::RatingTreeBindingStatus::EditLockInvalid:
            // 路由入口已经查过一次；能走到这里说明锁是在事务开始之后失效的
            // （过期或被管理员强制收回）。与保存草稿、确认入库同一个错误码。
            return {"edit_lock_invalid", "编辑锁已失效，本次修改未写入，请刷新页面。", 409};
        case db::RatingTreeBindingStatus::Conflict:
            // 结果自带错误码时优先用它：同一个 Conflict 下，"台账版本已变化"要求
            // 前端刷新，跟"年度已有正式评定"是两种完全不同的处置。
            return {
                outcome.error_code.empty() ? "component_binding_conflict" : outcome.error_code,
                !outcome.error_message.empty() ? outcome.error_message
                    : "台账未确认，或导入不在待校对阶段。",
                409};
        case db::RatingTreeBindingStatus::Invalid:
            return {
                outcome.error_code.empty() ? "invalid_component_binding" : outcome.error_code,
                !outcome.error_message.empty() ? outcome.error_message
                    : "绑定参数无效。",
                400};
        case db::RatingTreeBindingStatus::TreeNotFound:
            return {"rating_tree_not_found", "评定树版本不存在。", 404};
        case db::RatingTreeBindingStatus::TreeUnavailable:
            return {"rating_tree_unavailable",
                    "所选评定树尚未发布，或关联规范包当前不可用。", 409};
        case db::RatingTreeBindingStatus::MappingIncompatible:
            return {"rating_tree_inventory_incompatible",
                    "当前已确认台账无法完整继承到所选评定树，请先检查台账规范映射。", 409};
        default:
            return {"database_unavailable", "数据库暂不可用。", 503};
    }
}

// 注册一个写接口：POST 处理器 + 同路径的 OPTIONS 预检。两者成对，缺一不可。
//
// 只注册 POST 会怎样：这些接口都带 X-Edit-Lock-Token 头，浏览器因此先发 OPTIONS
// 预检；预检没有处理器就是 404，真正的 POST 根本发不出去。前端拿到的是 fetch 的
// 网络错误而不是 HTTP 响应，只能报一句笼统的"操作失败"，后端日志里则什么都没有,
// 排查毫无线索。此前预检路径是一份手工维护的清单，新加接口必须记得往里补一行——
// bind-multi 上线时就漏了。改成成对注册后漏不掉。
template <typename Handler>
void register_post_route(const std::string& path, Handler&& handler) {
    register_options_handler(path);
    drogon::app().registerHandler(path, std::forward<Handler>(handler), {drogon::Post});
}

void register_inspection_rating_tree_routes(const drogon::orm::DbClientPtr& db_client) {
    // 路径跟着职责走：这里绑的是年度评定树，不再是构件。旧的
    // component-binding 前缀下那九个构件绑定路由已随 4.0 链路一起下线。
    const std::string base = "/api/import-records/{import_id}/rating-tree-binding";
    // 只读接口的预检仍需单独注册：GET 带 Authorization 头，同样会触发预检。

    register_post_route(
        base,
        [db_client](const drogon::HttpRequestPtr& request,
                    HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor) {
                    respond_unauthorized(callback);
                    return;
                }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                const auto body = request->getJsonObject();
                if (body == nullptr ||
                    !(*body)["rating_tree_version_id"].isString() ||
                    !is_valid_uuid(
                        (*body)["rating_tree_version_id"].asString())) {
                    respond_json(callback, make_error_body(
                        "invalid_rating_tree_binding",
                        "必须选择有效的评定树版本。"),
                        drogon::k400BadRequest);
                    return;
                }
                std::string expected;
                if (!parse_expected_revision(body.get(), expected, callback,
                                             "invalid_rating_tree_binding")) return;
                respond_rating_tree_binding(
                    callback,
                    db::InspectionRatingTreeRepository(db_client).bind_rating_tree(
                        import_id,
                        (*body)["rating_tree_version_id"].asString(),
                        actor->id, expected,
                        edit_lock_from_request(request, *actor)));
            } catch (...) {
                respond_db_unavailable(callback);
            }
        });

    // 5.0：构件绑定、标记缺失、取消绑定、批量替换与区间展开已全部改走解析链路
    // （ImportResolutionRoutes）。这里原有的九个路由不再注册。
    //
    // 它们不是“没人调就算了”：那些写操作会往草稿 JSON 里写 bridge_component_id、
    // rating_tree_node_id 等 5.0 已删字段，调一次就把草稿写成非法契约，下一次读
    // 就被前端契约守卫整份拒掉。设计 §21 也明确禁止新关系表与旧 JSON 字段双写。
    //
    // 年度评定树绑定（上面那一个）不属于构件解析，照常保留。
}

}  // namespace bridge_report::http
