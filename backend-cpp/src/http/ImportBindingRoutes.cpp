#include "bridge_report/http/ImportBindingRoutes.hpp"

#include <string>

#include "bridge_report/db/ImportBindingRepository.hpp"
#include "bridge_report/db/ComponentRangeSplitRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/EditLockRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {

namespace {

Json::Value binding_component_json(const db::BindingComponentSummary& summary) {
    Json::Value value(Json::objectValue);
    value["entry_id"] = summary.entry_id;
    value["bridge_component_id"] = summary.bridge_component_id;
    value["component_number"] = summary.component_number;
    value["site_component_type"] = summary.site_component_type;
    value["site_name"] = summary.site_name;
    return value;
}

}  // namespace

Json::Value binding_overview_json(const db::BindingOverview& overview) {
    Json::Value value;
    value["inventory_confirmed"] = overview.inventory_confirmed;
    // 契约不变量：inventory_revision_id 非空 当且仅当 inventory_confirmed 为真。
    value["inventory_revision_id"] = overview.inventory_revision_id.has_value()
        ? Json::Value(*overview.inventory_revision_id) : Json::Value(Json::nullValue);
    value["rating_tree"] = Json::Value(Json::nullValue);
    if (overview.rating_tree.has_value()) {
        const auto& tree = *overview.rating_tree;
        value["rating_tree"]["version_id"] = tree.version_id;
        value["rating_tree"]["tree_name"] = tree.tree_name;
        value["rating_tree"]["package_version"] = tree.package_version;
        value["rating_tree"]["h21_package_version"] =
            tree.h21_package_version;
        value["rating_tree"]["maintenance_package_version"] =
            tree.maintenance_package_version;
    }
    value["groups"] = Json::Value(Json::arrayValue);
    for (const auto& group : overview.groups) {
        Json::Value group_json;
        group_json["part_name"] = group.part_name;
        group_json["total"] = group.total;
        group_json["bound"] = group.bound;
        group_json["unmatched"] = group.unmatched;
        group_json["ambiguous"] = group.ambiguous;
        group_json["missing"] = group.missing;
        group_json["rows"] = Json::Value(Json::arrayValue);
        for (const auto& row : group.rows) {
            Json::Value row_json;
            row_json["component_number"] = row.component_number;
            row_json["defect_count"] = row.defect_count;
            row_json["status"] = row.status;
            row_json["bridge_component_id"] = row.bridge_component_id.has_value()
                ? Json::Value(*row.bridge_component_id) : Json::Value(Json::nullValue);
            // candidate_component_ids 不再进 JSON：前端要的是可显示的构件信息，
            // 裸 id 只能靠拉整份台账去换。行状态仍由那批 id 在后端判定。
            row_json["bound_component"] = row.bound_component.has_value()
                ? binding_component_json(*row.bound_component) : Json::Value(Json::nullValue);
            row_json["candidate_components"] = Json::Value(Json::arrayValue);
            for (const auto& candidate : row.candidate_components) {
                row_json["candidate_components"].append(binding_component_json(candidate));
            }
            row_json["split_eligible"] = row.split_eligible;
            row_json["split_expanded_count"] = row.split_expanded_count
                ? Json::Value(*row.split_expanded_count) : Json::Value();
            group_json["rows"].append(std::move(row_json));
        }
        value["groups"].append(std::move(group_json));
    }
    return value;
}

namespace {

Json::Value split_analysis_json(const review::ComponentRangeSplitAnalysis& analysis) {
    Json::Value value(Json::objectValue);
    value["items"] = Json::Value(Json::arrayValue);
    for (const auto& item : analysis.items) {
        Json::Value row(Json::objectValue);
        row["part_name"] = item.target.part_name;
        row["component_number"] = item.target.component_number;
        row["expanded_component_count"] = item.expanded_component_count;
        row["source_defect_count"] = item.source_defect_count;
        row["result_defect_count"] = item.result_defect_count;
        row["result_photo_count"] = item.result_photo_count;
        row["bound_count"] = item.bound_count;
        row["ambiguous_count"] = item.ambiguous_count;
        row["unmatched_count"] = item.unmatched_count;
        value["items"].append(std::move(row));
    }
    const auto& totals = analysis.totals;
    value["totals"]["selected_range_count"] = totals.selected_range_count;
    value["totals"]["source_defect_count"] = totals.source_defect_count;
    value["totals"]["result_defect_count"] = totals.result_defect_count;
    value["totals"]["result_photo_count"] = totals.result_photo_count;
    value["totals"]["bound_count"] = totals.bound_count;
    value["totals"]["ambiguous_count"] = totals.ambiguous_count;
    value["totals"]["unmatched_count"] = totals.unmatched_count;
    return value;
}

void respond_split(const HttpCallback& callback,
                   const db::ComponentRangeSplitOutcome& outcome) {
    if (outcome.status == db::ComponentRangeSplitStatus::Ok) {
        Json::Value body = split_analysis_json(*outcome.analysis);
        body["impact_token"] = outcome.impact_token;
        if (!outcome.operation_id.empty()) body["operation_id"] = outcome.operation_id;
        if (outcome.overview) body["overview"] = binding_overview_json(*outcome.overview);
        respond_json(callback, body);
        return;
    }
    if (outcome.status == db::ComponentRangeSplitStatus::NotFound) {
        respond_import_record_not_found(callback);
        return;
    }
    if (outcome.status == db::ComponentRangeSplitStatus::EditLockInvalid) {
        respond_json(callback, make_error_body(
            "edit_lock_invalid", "编辑锁已失效，本次拆分未写入，请刷新页面。"),
            drogon::k409Conflict);
        return;
    }
    const auto code = !outcome.error_code.empty() ? outcome.error_code
        : outcome.status == db::ComponentRangeSplitStatus::Stale
            ? "component_range_split_stale"
        : outcome.status == db::ComponentRangeSplitStatus::Conflict
            ? "component_range_split_conflict"
            : "invalid_component_range_split";
    const auto message = !outcome.error_message.empty() ? outcome.error_message
        : outcome.status == db::ComponentRangeSplitStatus::Conflict
            ? "导入记录不在待校对阶段，或没有已确认构件台账。"
            : "所选构件范围无法拆分。";
    auto body = make_error_body(code, message);
    if (!outcome.rejected_target.component_number.empty()) {
        body["details"]["part_name"] = outcome.rejected_target.part_name;
        body["details"]["component_number"] =
            outcome.rejected_target.component_number;
    }
    const auto status = outcome.status == db::ComponentRangeSplitStatus::Conflict
            || outcome.status == db::ComponentRangeSplitStatus::Stale
        ? drogon::k409Conflict
        : outcome.status == db::ComponentRangeSplitStatus::Failed
            ? drogon::k503ServiceUnavailable : drogon::k400BadRequest;
    respond_json(callback, body, status);
}

bool parse_split_targets(
    const Json::Value* body,
    std::vector<review::ComponentRangeSplitTarget>& targets,
    const HttpCallback& callback) {
    if (body == nullptr || !(*body)["targets"].isArray()
        || (*body)["targets"].empty()) {
        respond_json(callback, make_error_body(
            "invalid_component_range_split", "targets 必须是非空数组。"),
            drogon::k400BadRequest);
        return false;
    }
    for (const auto& item : (*body)["targets"]) {
        if (!item.isObject() || !item["part_name"].isString()
            || !item["component_number"].isString()
            || item["part_name"].asString().empty()
            || item["component_number"].asString().empty()) {
            respond_json(callback, make_error_body(
                "invalid_component_range_split",
                "每个目标都需要非空的 part_name 与 component_number。"),
                drogon::k400BadRequest);
            return false;
        }
        targets.push_back(
            {item["part_name"].asString(), item["component_number"].asString()});
    }
    return true;
}

void respond_binding(const HttpCallback& callback, const db::BindingOutcome& outcome) {
    switch (outcome.status) {
        case db::BindingStatus::Ok: {
            Json::Value body;
            body["overview"] = binding_overview_json(*outcome.overview);
            respond_json(callback, body);
            return;
        }
        case db::BindingStatus::NotFound:
            respond_import_record_not_found(callback);
            return;
        case db::BindingStatus::EditLockInvalid:
            // 路由入口已经查过一次；能走到这里说明锁是在事务开始之后失效的
            // （过期或被管理员强制收回）。与保存草稿、确认入库同一个错误码。
            respond_json(callback, make_error_body(
                "edit_lock_invalid", "编辑锁已失效，本次修改未写入，请刷新页面。"),
                drogon::k409Conflict);
            return;
        case db::BindingStatus::Conflict: {
            // 批量绑定整批不写，必须让用户知道是哪一条挡住的。
            // 结果自带错误码时优先用它：同一个 Conflict 状态下，"台账版本已变化"要求
            // 前端刷新概览，跟"类别不符"是两种完全不同的处置。
            auto body = make_error_body(
                outcome.error_code.empty()
                    ? "component_binding_conflict" : outcome.error_code,
                !outcome.error_message.empty() ? outcome.error_message
                : outcome.rejected_component_number.empty()
                    ? "台账未确认、导入不在待校对阶段，或所选构件类别与部件名称不符。"
                    : "构件 " + outcome.rejected_component_number
                        + " 的类别与部件名称不符，整批未应用。");
            if (!outcome.rejected_component_number.empty()) {
                body["details"]["rejected_component_number"] = outcome.rejected_component_number;
            }
            respond_json(callback, body, drogon::k409Conflict);
            return;
        }
        case db::BindingStatus::Invalid: {
            auto body = make_error_body(
                "invalid_component_binding",
                outcome.rejected_component_number.empty()
                    ? "绑定参数无效或未找到该编号。"
                    : "构件编号 " + outcome.rejected_component_number
                        + " 不在本次导入中，整批未应用。");
            if (!outcome.rejected_component_number.empty()) {
                body["details"]["rejected_component_number"] = outcome.rejected_component_number;
            }
            respond_json(callback, body, drogon::k400BadRequest);
            return;
        }
        case db::BindingStatus::TreeNotFound:
            respond_json(callback, make_error_body(
                "rating_tree_not_found", "评定树版本不存在。"),
                drogon::k404NotFound);
            return;
        case db::BindingStatus::TreeUnavailable:
            respond_json(callback, make_error_body(
                "rating_tree_unavailable",
                "所选评定树尚未发布，或关联规范包当前不可用。"),
                drogon::k409Conflict);
            return;
        case db::BindingStatus::MappingIncompatible:
            respond_json(callback, make_error_body(
                "rating_tree_inventory_incompatible",
                "当前已确认台账无法完整继承到所选评定树，请先检查台账规范映射。"),
                drogon::k409Conflict);
            return;
        default:
            respond_db_unavailable(callback);
            return;
    }
}

void respond_rating_tree_binding(
    const HttpCallback& callback,
    const db::BindingOutcome& outcome) {
    if (outcome.status == db::BindingStatus::Conflict) {
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

// 取回 part_name + component_number（bind 另需 bridge_component_id）。
bool parse_target(const Json::Value* body, std::string& part_name, std::string& number,
                  const HttpCallback& callback) {
    if (body == nullptr || !(*body)["part_name"].isString() || !(*body)["component_number"].isString()) {
        respond_json(callback, make_error_body(
            "invalid_component_binding", "部件名称与构件编号必须是文本。"),
            drogon::k400BadRequest);
        return false;
    }
    part_name = (*body)["part_name"].asString();
    number = (*body)["component_number"].asString();
    if (part_name.empty() || number.empty()) {
        respond_json(callback, make_error_body(
            "invalid_component_binding", "部件名称与构件编号不能为空。"),
            drogon::k400BadRequest);
        return false;
    }
    return true;
}

}  // namespace

void register_import_binding_routes(const drogon::orm::DbClientPtr& db_client) {
    const std::string base = "/api/import-records/{import_id}/component-binding";
    for (const auto& path : {base, base + "/bind", base + "/bind-batch",
                             base + "/rating-tree",
                             base + "/mark-missing", base + "/clear",
                             base + "/inventory",
                             base + "/split-preview", base + "/split-apply"}) {
        register_options_handler(path);
    }

    drogon::app().registerHandler(
        base + "/rating-tree",
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
                    db::ImportBindingRepository(db_client).bind_rating_tree(
                        import_id,
                        (*body)["rating_tree_version_id"].asString(),
                        actor->id, expected,
                        edit_lock_from_request(request, *actor)));
            } catch (...) {
                respond_db_unavailable(callback);
            }
        }, {drogon::Post});

    // 批量替换取数。GET，只校验版本、绝不锁定：打开一次对话框就把年度锁死，
    // 是任何人都不会预期的副作用，浏览器预取或重试还会重复触发。
    drogon::app().registerHandler(
        base + "/inventory",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            const auto expected = request->getParameter("expected_inventory_revision_id");
            if (!is_valid_uuid(expected)) {
                respond_json(callback, make_error_body(
                    "invalid_component_binding_inventory_request",
                    "expected_inventory_revision_id 必须是有效的台账版本 UUID。"),
                    drogon::k400BadRequest);
                return;
            }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback); return;
                }
                const auto outcome =
                    db::ImportBindingRepository(db_client).load_replace_inventory(
                        import_id, expected);
                switch (outcome.status) {
                    case db::BindingStatus::NotFound:
                        respond_import_record_not_found(callback); return;
                    case db::BindingStatus::Conflict:
                        respond_json(callback, make_error_body(
                            outcome.error_code.empty()
                                ? "component_binding_conflict" : outcome.error_code,
                            outcome.error_message.empty()
                                ? "导入记录不在待校对阶段。" : outcome.error_message),
                            drogon::k409Conflict);
                        return;
                    case db::BindingStatus::Failed:
                        respond_db_unavailable(callback); return;
                    default: break;
                }
                Json::Value body;
                body["inventory_revision_id"] = outcome.replace_revision_id;
                body["entries"] = Json::Value(Json::arrayValue);
                for (const auto& entry : outcome.replace_entries) {
                    Json::Value item(Json::objectValue);
                    item["bridge_component_id"] = entry.bridge_component_id;
                    item["component_number"] = entry.component_number;
                    // 服务端已经过滤掉停用构件，字段仍然要返回：前端预览里有一句
                    // if (!entry.is_active) continue，缺字段时 !undefined 为真，
                    // 会把每一条都跳过，预览安静地全判成"台账里没有"。
                    item["is_active"] = entry.is_active;
                    body["entries"].append(std::move(item));
                }
                respond_json(callback, body);
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Get});

    drogon::app().registerHandler(
        base + "/split-preview",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            try {
                if (!authenticate_request(db_client, request)) {
                    respond_unauthorized(callback); return;
                }
                const auto body = request->getJsonObject();
                std::vector<review::ComponentRangeSplitTarget> targets;
                if (!parse_split_targets(body.get(), targets, callback)) return;
                std::string expected;
                if (!parse_expected_revision(body.get(), expected, callback,
                                             "invalid_component_range_split")) return;
                respond_split(callback,
                    db::ComponentRangeSplitRepository(db_client).preview(
                        import_id, targets, expected));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        base + "/split-apply",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor) { respond_unauthorized(callback); return; }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                const auto body = request->getJsonObject();
                std::vector<review::ComponentRangeSplitTarget> targets;
                if (!parse_split_targets(body.get(), targets, callback)) return;
                if (!(*body)["impact_token"].isString()
                    || (*body)["impact_token"].asString().empty()) {
                    respond_json(callback, make_error_body(
                        "component_range_split_impact_token_required",
                        "应用拆分前必须提供预览影响令牌。"),
                        drogon::k400BadRequest);
                    return;
                }
                std::string expected;
                if (!parse_expected_revision(body.get(), expected, callback,
                                             "invalid_component_range_split")) return;
                respond_split(callback,
                    db::ComponentRangeSplitRepository(db_client).apply(
                        import_id, targets, (*body)["impact_token"].asString(),
                        actor->id, expected,
                        edit_lock_from_request(request, *actor)));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    // 批量绑定：供绑定界面的"批量替换"。单次读改写，任一目标非法则整批不写。
    drogon::app().registerHandler(
        base + "/bind-batch",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor.has_value()) { respond_unauthorized(callback); return; }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                const auto body = request->getJsonObject();
                if (body == nullptr || !(*body)["targets"].isArray()
                    || (*body)["targets"].empty()) {
                    respond_json(callback, make_error_body(
                        "invalid_component_binding", "targets 必须是非空数组。"),
                        drogon::k400BadRequest);
                    return;
                }
                std::vector<db::BindingTarget> targets;
                for (const auto& item : (*body)["targets"]) {
                    if (!item.isObject() || !item["part_name"].isString()
                        || !item["component_number"].isString()
                        || !item["bridge_component_id"].isString()) {
                        respond_json(callback, make_error_body(
                            "invalid_component_binding",
                            "每个目标都需要 part_name、component_number 与 bridge_component_id。"),
                            drogon::k400BadRequest);
                        return;
                    }
                    targets.push_back({item["part_name"].asString(),
                                       item["component_number"].asString(),
                                       item["bridge_component_id"].asString()});
                }
                std::string expected;
                if (!parse_expected_revision(body.get(), expected, callback)) return;
                respond_binding(callback,
                    db::ImportBindingRepository(db_client).bind_batch(
                        import_id, targets, expected,
                        edit_lock_from_request(request, *actor)));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        base,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback); return;
                }
                respond_binding(callback, db::ImportBindingRepository(db_client).overview(import_id));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Get});

    drogon::app().registerHandler(
        base + "/bind",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor.has_value()) { respond_unauthorized(callback); return; }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                const auto body = request->getJsonObject();
                std::string part_name, number;
                if (!parse_target(body.get(), part_name, number, callback)) return;
                if (!(*body)["bridge_component_id"].isString()
                    || (*body)["bridge_component_id"].asString().empty()) {
                    respond_json(callback, make_error_body(
                        "invalid_component_binding", "必须选择实际构件。"),
                        drogon::k400BadRequest); return;
                }
                std::string expected;
                if (!parse_expected_revision(body.get(), expected, callback)) return;
                respond_binding(callback, db::ImportBindingRepository(db_client).bind(
                    import_id, part_name, number,
                    (*body)["bridge_component_id"].asString(), expected,
                    edit_lock_from_request(request, *actor)));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        base + "/mark-missing",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor.has_value()) { respond_unauthorized(callback); return; }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                const auto body = request->getJsonObject();
                std::string part_name, number;
                if (!parse_target(body.get(), part_name, number, callback)) return;
                std::string expected;
                if (!parse_expected_revision(body.get(), expected, callback)) return;
                respond_binding(callback, db::ImportBindingRepository(db_client).mark_missing(
                    import_id, part_name, number, expected,
                    edit_lock_from_request(request, *actor)));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});

    drogon::app().registerHandler(
        base + "/clear",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) { respond_import_record_not_found(callback); return; }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor.has_value()) { respond_unauthorized(callback); return; }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                const auto body = request->getJsonObject();
                std::string part_name, number;
                if (!parse_target(body.get(), part_name, number, callback)) return;
                std::string expected;
                if (!parse_expected_revision(body.get(), expected, callback)) return;
                respond_binding(callback, db::ImportBindingRepository(db_client).clear(
                    import_id, part_name, number, expected,
                    edit_lock_from_request(request, *actor)));
            } catch (...) { respond_db_unavailable(callback); }
        }, {drogon::Post});
}

}  // namespace bridge_report::http
