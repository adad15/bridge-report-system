#include "bridge_report/http/ImportResolutionRoutes.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/EditLockRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"

namespace bridge_report::http {
namespace {

Json::Value optional_string_json(const std::optional<std::string>& value) {
    return value.has_value() ? Json::Value(*value) : Json::Value(Json::nullValue);
}

Json::Value string_list_json(const std::vector<std::string>& values) {
    Json::Value list(Json::arrayValue);
    for (const auto& value : values) list.append(value);
    return list;
}

Json::Value component_summary_json(
    const resolution::WorkspaceComponentSummary& summary) {
    Json::Value value(Json::objectValue);
    value["bridge_component_id"] = summary.bridge_component_id;
    value["component_number"] = summary.component_number;
    value["site_component_type"] = summary.site_component_type;
    value["standard_component_category_id"] = summary.standard_component_category_id;
    value["standard_bridge_type_id"] = summary.standard_bridge_type_id;
    value["site_name"] = summary.site_name;
    return value;
}

Json::Value instance_json(const resolution::WorkspaceDefectInstance& instance) {
    Json::Value value(Json::objectValue);
    // 身份字段一律显式命名。来源病害用 source_candidate_id、实例用
    // resolved_defect_instance_id，绝不出现含糊的 defect_id（§22.2）。
    value["resolved_defect_instance_id"] = instance.instance_id;
    value["target_id"] = instance.target_id;
    value["bridge_component_id"] = instance.bridge_component_id;
    value["instance_order"] = instance.instance_order;
    value["instance_status"] = instance.instance_status;
    value["is_photo_owner"] = instance.is_photo_owner;
    value["version"] = instance.version;
    value["component_resolution_version"] = instance.component_resolution_version;
    value["overridden_fields"] = string_list_json(instance.overridden_fields);
    value["effective_facts"] = instance.effective_facts;

    Json::Value rating(Json::objectValue);
    rating["present"] = instance.has_rating;
    rating["status"] = instance.has_rating ? Json::Value(instance.rating_status)
                                           : Json::Value(Json::nullValue);
    rating["rating_tree_node_id"] = optional_string_json(instance.rating_tree_node_id);
    rating["match_method"] = optional_string_json(instance.rating_match_method);
    rating["version"] = instance.rating_version;
    rating["content_changed_after_manual_resolution"] =
        instance.content_changed_after_manual_resolution;
    value["rating_resolution"] = std::move(rating);
    return value;
}

Json::Value group_json(const resolution::WorkspaceComponentGroup& group) {
    Json::Value value(Json::objectValue);
    value["group_id"] = group.group_id;
    value["source_component_name"] = group.source_component_name;
    value["source_component_number"] = optional_string_json(group.source_component_number);
    value["normalized_component_number"] = group.normalized_component_number;
    value["resolution_mode"] = group.resolution_mode;
    value["status"] = group.status;
    value["match_method"] = optional_string_json(group.match_method);
    value["inventory_revision_id"] = optional_string_json(group.inventory_revision_id);
    value["version"] = group.version;
    // 派生标签，不是入库状态：前端照它渲染"歧义"计数，不自己按候选数再算一遍。
    value["ambiguous"] = group.ambiguous;
    value["split_eligible"] = group.split_eligible;
    value["split_expanded_count"] = group.split_expanded_count.has_value()
        ? Json::Value(*group.split_expanded_count) : Json::Value(Json::nullValue);

    value["targets"] = Json::Value(Json::arrayValue);
    for (const auto& target : group.targets) {
        value["targets"].append(component_summary_json(target));
    }
    value["candidates"] = Json::Value(Json::arrayValue);
    for (const auto& candidate : group.candidates) {
        value["candidates"].append(component_summary_json(candidate));
    }
    value["members"] = Json::Value(Json::arrayValue);
    for (const auto& member : group.members) {
        Json::Value member_json(Json::objectValue);
        member_json["member_id"] = member.member_id;
        member_json["source_candidate_id"] = member.source_candidate_id;
        member_json["source_order"] = member.source_order;
        member_json["instances"] = Json::Value(Json::arrayValue);
        for (const auto& instance : member.instances) {
            member_json["instances"].append(instance_json(instance));
        }
        value["members"].append(std::move(member_json));
    }
    if (group.side_pair_option.has_value()) {
        Json::Value option(Json::objectValue);
        option["label"] = group.side_pair_option->label;
        option["bridge_component_ids"] = Json::Value(Json::arrayValue);
        for (const auto& id : group.side_pair_option->bridge_component_ids) {
            option["bridge_component_ids"].append(id);
        }
        value["side_pair_option"] = std::move(option);
    } else {
        value["side_pair_option"] = Json::Value(Json::nullValue);
    }
    value["allowed_actions"] = string_list_json(group.allowed_actions);
    value["blocked_reasons"] = string_list_json(group.blocked_reasons);
    return value;
}

Json::Value build_command_result_json(
    const resolution::ResolutionCommandResult& result) {
    Json::Value value(Json::objectValue);
    value["affected_groups"] = Json::Value(Json::arrayValue);
    for (const auto& group : result.affected_groups) {
        value["affected_groups"].append(group_json(group));
    }
    Json::Value progress(Json::objectValue);
    progress["group_count"] = result.progress.group_count;
    progress["bound_count"] = result.progress.bound_count;
    progress["unresolved_count"] = result.progress.unresolved_count;
    progress["ambiguous_count"] = result.progress.ambiguous_count;
    progress["missing_count"] = result.progress.missing_count;
    progress["instance_count"] = result.progress.instance_count;
    progress["active_instance_count"] = result.progress.active_instance_count;
    progress["rating_matched_count"] = result.progress.rating_matched_count;
    progress["rating_unresolved_count"] = result.progress.rating_unresolved_count;
    progress["rating_missing_count"] = result.progress.rating_missing_count;
    value["progress"] = std::move(progress);
    return value;
}

/// 写命令共用的收尾：成功回受影响对象，失败按错误码映射状态。
void respond_command(
    const HttpCallback& callback, const resolution::ResolutionOutcome& outcome) {
    if (outcome.status != resolution::ResolutionStatus::Ok ||
        !outcome.command_result.has_value()) {
        const auto error = resolution_error_response(outcome);
        respond_json(
            callback,
            make_error_body(error.error_code, error.error_message),
            static_cast<drogon::HttpStatusCode>(error.http_status));
        return;
    }
    respond_json(callback, build_command_result_json(*outcome.command_result));
}

bool read_expected_version(
    const Json::Value& body, int& expected_version, const HttpCallback& callback) {
    if (!body["expected_version"].isInt()) {
        respond_json(
            callback,
            make_error_body("invalid_resolution_request", "缺少 expected_version。"),
            drogon::k400BadRequest);
        return false;
    }
    expected_version = body["expected_version"].asInt();
    return true;
}

std::string optional_body_string(const Json::Value& body, const char* key) {
    return body[key].isString() ? body[key].asString() : std::string{};
}

/// 解析 `If-Match: "draft-<version>"`。缺失或格式不对时返回 nullopt。
///
/// 用标准头而不是塞进请求体，是为了不把并发元数据混进 5.0 合同：`saveReviewDraft`
/// 的请求体必须仍然是一份纯粹的 BridgeAnnualInspectionData（§11.1）。
std::optional<int> parse_if_match_draft_version(const drogon::HttpRequestPtr& request) {
    auto value = request->getHeader("if-match");
    if (value.empty()) value = request->getHeader("If-Match");
    if (value.size() < 3) return std::nullopt;
    if (value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    constexpr std::string_view prefix = "draft-";
    if (value.rfind(prefix, 0) != 0) return std::nullopt;
    const auto digits = value.substr(prefix.size());
    if (digits.empty()) return std::nullopt;
    for (const char character : digits) {
        if (!std::isdigit(static_cast<unsigned char>(character))) return std::nullopt;
    }
    try {
        return std::stoi(digits);
    } catch (...) {
        return std::nullopt;
    }
}

Json::Value plan_preview_json(const resolution::ResolutionPlanPreview& plan) {
    Json::Value value(Json::objectValue);
    value["plan_token"] = plan.plan_token;
    value["operation_type"] = plan.operation_type;
    value["expires_at"] = plan.expires_at;
    value["will_apply_count"] = plan.will_apply_count;
    value["skipped_count"] = plan.skipped_count;
    value["blocked_count"] = plan.blocked_count;
    value["instances_before"] = plan.instances_before;
    value["instances_after"] = plan.instances_after;
    value["rating_recomputed_count"] = plan.rating_recomputed_count;
    value["inventory_revision_id"] = optional_string_json(plan.inventory_revision_id);
    value["rating_tree_version_id"] = optional_string_json(plan.rating_tree_version_id);
    value["rows"] = Json::Value(Json::arrayValue);
    for (const auto& row : plan.rows) {
        Json::Value row_json(Json::objectValue);
        row_json["group_id"] = row.group_id;
        row_json["source_component_name"] = row.source_component_name;
        row_json["source_component_number"] = row.source_component_number;
        row_json["member_count"] = row.member_count;
        row_json["resolved_numbers"] = string_list_json(row.resolved_numbers);
        row_json["target_component_ids"] = string_list_json(row.target_component_ids);
        row_json["outcome"] = row.outcome;
        // 行级原因码由后端给：前端不自己拼话术，也不自己判断为什么跳过。
        row_json["reason_code"] = row.reason_code;
        row_json["reason_message"] = row.reason_message;
        value["rows"].append(std::move(row_json));
    }
    return value;
}

Json::Value manual_defect_json(const resolution::ManualDefectResult& result) {
    Json::Value value(Json::objectValue);
    // 新来源病害要先并进本地草稿，再接受新的 draft_version——只更新版本却保留缺少
    // 新候选的旧草稿，下一次整份保存就会把它当成"用户删掉了"（§16.1）。
    value["source_defect"] = result.source_defect;
    value["draft_version"] = result.draft_version;
    value["result"] = build_command_result_json(result.command_result);
    return value;
}

}  // namespace

Json::Value resolution_workspace_json(
    const resolution::ResolutionWorkspace& workspace) {
    Json::Value value(Json::objectValue);
    value["import_record_id"] = workspace.import_record_id;
    value["bridge_id"] = workspace.bridge_id;
    value["draft_version"] = workspace.draft_version;
    value["inventory_confirmed"] = workspace.inventory_confirmed;
    // 契约不变量：inventory_revision_id 非空 当且仅当 inventory_confirmed 为真。
    value["inventory_revision_id"] = optional_string_json(workspace.inventory_revision_id);

    value["rating_tree"] = Json::Value(Json::nullValue);
    if (workspace.rating_tree.has_value()) {
        Json::Value tree(Json::objectValue);
        tree["version_id"] = workspace.rating_tree->version_id;
        tree["tree_name"] = workspace.rating_tree->tree_name;
        tree["package_version"] = workspace.rating_tree->package_version;
        tree["h21_package_version"] = workspace.rating_tree->h21_package_version;
        tree["maintenance_package_version"] =
            workspace.rating_tree->maintenance_package_version;
        value["rating_tree"] = std::move(tree);
    }

    value["groups"] = Json::Value(Json::arrayValue);
    for (const auto& group : workspace.groups) value["groups"].append(group_json(group));

    // 部件层级是读模型派生物（§4.7）：批量替换入口与分组表头挂在它上面，
    // 但关系表只建行级组。
    value["parts"] = Json::Value(Json::arrayValue);
    for (const auto& part : workspace.parts) {
        Json::Value part_json(Json::objectValue);
        part_json["source_component_name"] = part.source_component_name;
        part_json["total"] = part.total;
        part_json["bound"] = part.bound;
        part_json["unresolved"] = part.unresolved;
        part_json["ambiguous"] = part.ambiguous;
        part_json["missing"] = part.missing;
        part_json["group_ids"] = string_list_json(part.group_ids);
        value["parts"].append(std::move(part_json));
    }

    Json::Value progress(Json::objectValue);
    progress["group_count"] = workspace.progress.group_count;
    progress["bound_count"] = workspace.progress.bound_count;
    progress["unresolved_count"] = workspace.progress.unresolved_count;
    progress["ambiguous_count"] = workspace.progress.ambiguous_count;
    progress["missing_count"] = workspace.progress.missing_count;
    progress["instance_count"] = workspace.progress.instance_count;
    progress["active_instance_count"] = workspace.progress.active_instance_count;
    progress["rating_matched_count"] = workspace.progress.rating_matched_count;
    progress["rating_unresolved_count"] = workspace.progress.rating_unresolved_count;
    progress["rating_missing_count"] = workspace.progress.rating_missing_count;
    value["progress"] = std::move(progress);

    return value;
}

Json::Value resolution_command_result_json(
    const resolution::ResolutionCommandResult& result) {
    return build_command_result_json(result);
}

Json::Value resolution_manual_defect_json(
    const resolution::ManualDefectResult& result) {
    return manual_defect_json(result);
}

ResolutionErrorResponse resolution_error_response(
    const resolution::ResolutionOutcome& outcome) {
    switch (outcome.status) {
    case resolution::ResolutionStatus::Ok:
        return {"", "", 200};
    case resolution::ResolutionStatus::NotFound:
        return {"import_record_not_found", "导入记录不存在。", 404};
    case resolution::ResolutionStatus::Conflict:
        return {
            outcome.error_code.empty() ? "import_record_wrong_status" : outcome.error_code,
            outcome.error_message.empty() ? "导入记录不在待校对状态。" : outcome.error_message,
            409};
    case resolution::ResolutionStatus::VersionConflict:
        return {"resolution_version_conflict", "解析状态版本已过期，请刷新冲突对象。", 409};
    case resolution::ResolutionStatus::EditLockInvalid:
        return {"edit_lock_invalid", "编辑锁已失效，请刷新页面重新获取编辑权。", 409};
    case resolution::ResolutionStatus::Invalid:
        return {
            outcome.error_code.empty() ? "invalid_resolution_request" : outcome.error_code,
            outcome.error_message.empty() ? "请求参数无效。" : outcome.error_message,
            400};
    case resolution::ResolutionStatus::Failed:
        break;
    }
    return {"database_unavailable", "数据库暂时不可用，请稍后重试。", 503};
}

namespace {

/// 注册一个写接口：PUT 处理器 + 同路径的 OPTIONS 预检。两者成对，缺一不可。
///
/// 只注册 PUT 会怎样：这些接口都带 X-Edit-Lock-Token 头，浏览器因此先发 OPTIONS
/// 预检；预检没有处理器就是 404，真正的 PUT 根本发不出去。前端拿到的是 fetch 的
/// 网络错误而不是 HTTP 响应，只能报一句笼统的"操作失败"，后端日志里什么都没有。
template <typename Handler>
void register_put_route(const std::string& path, Handler&& handler) {
    register_options_handler(path);
    drogon::app().registerHandler(path, std::forward<Handler>(handler), {drogon::Put});
}

}  // namespace

void register_import_resolution_routes(const drogon::orm::DbClientPtr& db_client) {
    const std::string import_base = "/api/import-records/{import_id}";
    const std::string base = import_base + "/resolution-workspace";
    // 只读接口的预检也要注册：GET 带 Authorization 头，同样会触发浏览器预检，
    // 漏了它前端拿到的是 fetch 网络错误而不是 HTTP 响应，后端日志里什么都没有。
    register_options_handler(base);

    register_put_route(
        import_base + "/component-groups/{group_id}/resolution",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id, const std::string& group_id) {
            if (!is_valid_uuid(import_id) || !is_valid_uuid(group_id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor) { respond_unauthorized(callback); return; }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                const auto body = request->getJsonObject();
                if (body == nullptr) {
                    respond_json(callback, make_error_body(
                        "invalid_resolution_request", "请求体必须是 JSON 对象。"),
                        drogon::k400BadRequest);
                    return;
                }
                resolution::ComponentResolutionRequest command;
                command.context.import_record_id = import_id;
                command.context.actor_user_id = actor->id;
                command.context.edit_lock = edit_lock_from_request(request, *actor);
                command.context.expected_inventory_revision_id =
                    optional_body_string(*body, "expected_inventory_revision_id");
                command.group_id = group_id;
                command.action = optional_body_string(*body, "action");
                if (!read_expected_version(*body, command.expected_version, callback)) {
                    return;
                }
                if ((*body)["targets"].isArray()) {
                    for (const auto& target : (*body)["targets"]) {
                        resolution::ResolutionTargetSelection selection;
                        selection.bridge_component_id =
                            optional_body_string(target, "bridge_component_id");
                        const auto role = optional_body_string(target, "target_role");
                        if (!role.empty()) selection.target_role = role;
                        if (!is_valid_uuid(selection.bridge_component_id)) {
                            respond_json(callback, make_error_body(
                                "invalid_resolution_request", "目标构件 id 无效。"),
                                drogon::k400BadRequest);
                            return;
                        }
                        command.targets.push_back(std::move(selection));
                    }
                }
                respond_command(
                    callback,
                    resolution::ImportResolutionService(db_client)
                        .apply_component_resolution(command));
            } catch (...) {
                respond_db_unavailable(callback);
            }
        });

    register_put_route(
        import_base + "/defect-instances/{instance_id}/rating-resolution",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id, const std::string& instance_id) {
            if (!is_valid_uuid(import_id) || !is_valid_uuid(instance_id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor) { respond_unauthorized(callback); return; }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                const auto body = request->getJsonObject();
                if (body == nullptr) {
                    respond_json(callback, make_error_body(
                        "invalid_resolution_request", "请求体必须是 JSON 对象。"),
                        drogon::k400BadRequest);
                    return;
                }
                resolution::RatingResolutionRequest command;
                command.context.import_record_id = import_id;
                command.context.actor_user_id = actor->id;
                command.context.edit_lock = edit_lock_from_request(request, *actor);
                command.instance_id = instance_id;
                command.rating_tree_node_id =
                    optional_body_string(*body, "rating_tree_node_id");
                command.expected_rating_tree_version_id =
                    optional_body_string(*body, "expected_rating_tree_version_id");
                if (!read_expected_version(*body, command.expected_version, callback)) {
                    return;
                }
                respond_command(
                    callback,
                    resolution::ImportResolutionService(db_client)
                        .apply_rating_resolution(command));
            } catch (...) {
                respond_db_unavailable(callback);
            }
        });

    register_put_route(
        import_base + "/defect-instances/{instance_id}/fact-overrides",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id, const std::string& instance_id) {
            if (!is_valid_uuid(import_id) || !is_valid_uuid(instance_id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor) { respond_unauthorized(callback); return; }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                const auto body = request->getJsonObject();
                if (body == nullptr) {
                    respond_json(callback, make_error_body(
                        "invalid_resolution_request", "请求体必须是 JSON 对象。"),
                        drogon::k400BadRequest);
                    return;
                }
                resolution::FactOverrideRequest command;
                command.context.import_record_id = import_id;
                command.context.actor_user_id = actor->id;
                command.context.edit_lock = edit_lock_from_request(request, *actor);
                command.instance_id = instance_id;
                if ((*body)["overrides"].isObject()) {
                    command.overrides = (*body)["overrides"];
                }
                // 清除是删键，不是写 null：两者在接口上必须分得开，否则"把备注清空"
                // 和"这个字段恢复来源值"就成了同一个请求。
                if ((*body)["cleared_fields"].isArray()) {
                    for (const auto& field : (*body)["cleared_fields"]) {
                        if (field.isString()) {
                            command.cleared_fields.push_back(field.asString());
                        }
                    }
                }
                if (!read_expected_version(*body, command.expected_version, callback)) {
                    return;
                }
                respond_command(
                    callback,
                    resolution::ImportResolutionService(db_client)
                        .apply_fact_overrides(command));
            } catch (...) {
                respond_db_unavailable(callback);
            }
        });

    register_put_route(
        import_base + "/defect-instances/{instance_id}/status",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id, const std::string& instance_id) {
            if (!is_valid_uuid(import_id) || !is_valid_uuid(instance_id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor) { respond_unauthorized(callback); return; }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                const auto body = request->getJsonObject();
                if (body == nullptr) {
                    respond_json(callback, make_error_body(
                        "invalid_resolution_request", "请求体必须是 JSON 对象。"),
                        drogon::k400BadRequest);
                    return;
                }
                resolution::InstanceStatusRequest command;
                command.context.import_record_id = import_id;
                command.context.actor_user_id = actor->id;
                command.context.edit_lock = edit_lock_from_request(request, *actor);
                command.instance_id = instance_id;
                command.instance_status = optional_body_string(*body, "instance_status");
                if (!read_expected_version(*body, command.expected_version, callback)) {
                    return;
                }
                respond_command(
                    callback,
                    resolution::ImportResolutionService(db_client)
                        .apply_instance_status(command));
            } catch (...) {
                respond_db_unavailable(callback);
            }
        });

    register_options_handler(import_base + "/resolution-plans");
    drogon::app().registerHandler(
        import_base + "/resolution-plans",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor) { respond_unauthorized(callback); return; }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                const auto body = request->getJsonObject();
                if (body == nullptr) {
                    respond_json(callback, make_error_body(
                        "invalid_resolution_request", "请求体必须是 JSON 对象。"),
                        drogon::k400BadRequest);
                    return;
                }
                resolution::ResolutionCommandContext context;
                context.import_record_id = import_id;
                context.actor_user_id = actor->id;
                context.edit_lock = edit_lock_from_request(request, *actor);
                context.expected_inventory_revision_id =
                    optional_body_string(*body, "expected_inventory_revision_id");

                const resolution::ImportResolutionService service(db_client);
                const auto operation_type = optional_body_string(*body, "operation_type");
                resolution::ResolutionOutcome outcome;
                if (operation_type == "bulk_replace") {
                    resolution::BulkReplaceIntent intent;
                    intent.source_component_name =
                        optional_body_string(*body, "source_component_name");
                    intent.find = optional_body_string(*body, "find");
                    intent.replace = optional_body_string(*body, "replace");
                    outcome = service.build_bulk_replace_plan(context, intent);
                } else if (operation_type == "range_expand") {
                    resolution::RangeExpandIntent intent;
                    for (const auto& id : (*body)["group_ids"]) {
                        if (id.isString()) intent.group_ids.push_back(id.asString());
                    }
                    outcome = service.build_range_expand_plan(context, intent);
                } else if (operation_type == "inventory_repoint") {
                    resolution::InventoryRepointIntent intent;
                    for (const auto& id : (*body)["group_ids"]) {
                        if (id.isString()) intent.group_ids.push_back(id.asString());
                    }
                    outcome = service.build_inventory_repoint_plan(context, intent);
                } else {
                    respond_json(callback, make_error_body(
                        "invalid_resolution_request", "未知的批量操作类型。"),
                        drogon::k400BadRequest);
                    return;
                }

                if (outcome.status != resolution::ResolutionStatus::Ok ||
                    !outcome.plan.has_value()) {
                    const auto error = resolution_error_response(outcome);
                    respond_json(
                        callback,
                        make_error_body(error.error_code, error.error_message),
                        static_cast<drogon::HttpStatusCode>(error.http_status));
                    return;
                }
                respond_json(callback, plan_preview_json(*outcome.plan),
                             drogon::k201Created);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    register_options_handler(import_base + "/resolution-plans/{plan_token}/apply");
    drogon::app().registerHandler(
        import_base + "/resolution-plans/{plan_token}/apply",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id, const std::string& plan_token) {
            if (!is_valid_uuid(import_id) || !is_valid_uuid(plan_token)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor) { respond_unauthorized(callback); return; }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                resolution::ResolutionCommandContext context;
                context.import_record_id = import_id;
                context.actor_user_id = actor->id;
                context.edit_lock = edit_lock_from_request(request, *actor);

                const auto outcome = resolution::ImportResolutionService(db_client)
                                         .apply_resolution_plan(context, plan_token);
                if (outcome.status != resolution::ResolutionStatus::Ok) {
                    const auto error = resolution_error_response(outcome);
                    respond_json(
                        callback,
                        make_error_body(error.error_code, error.error_message),
                        static_cast<drogon::HttpStatusCode>(error.http_status));
                    return;
                }
                Json::Value value(Json::objectValue);
                value["apply_result"] = outcome.apply_result.value_or(
                    Json::Value(Json::objectValue));
                // 重放已成功计划时没有受影响对象可回：那次写入早已完成，
                // 客户端要的是同一份首次结果，不是再执行一遍。
                value["result"] = outcome.command_result.has_value()
                    ? build_command_result_json(*outcome.command_result)
                    : Json::Value(Json::nullValue);
                respond_json(callback, value);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    // POST 而不是 PUT：它创建一条新的来源病害，不是幂等地替换某个已知资源。
    register_options_handler(import_base + "/manual-defects");
    drogon::app().registerHandler(
        import_base + "/manual-defects",
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& import_id) {
            if (!is_valid_uuid(import_id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                const auto actor = authenticate_request(db_client, request);
                if (!actor) { respond_unauthorized(callback); return; }
                if (!require_active_edit_lock(
                        db_client, request, import_id, *actor, callback)) return;
                const auto body = request->getJsonObject();
                if (body == nullptr) {
                    respond_json(callback, make_error_body(
                        "invalid_resolution_request", "请求体必须是 JSON 对象。"),
                        drogon::k400BadRequest);
                    return;
                }
                const auto draft_version = parse_if_match_draft_version(request);
                if (!draft_version.has_value()) {
                    respond_json(callback, make_error_body(
                        "invalid_resolution_request",
                        "缺少 If-Match: \"draft-<version>\" 请求头。"),
                        drogon::k428PreconditionRequired);
                    return;
                }
                resolution::ManualDefectRequest command;
                command.context.import_record_id = import_id;
                command.context.actor_user_id = actor->id;
                command.context.edit_lock = edit_lock_from_request(request, *actor);
                command.context.expected_inventory_revision_id =
                    optional_body_string(*body, "expected_inventory_revision_id");
                command.expected_draft_version = *draft_version;
                command.bridge_component_id =
                    optional_body_string(*body, "bridge_component_id");
                command.rating_tree_node_id =
                    optional_body_string(*body, "rating_tree_node_id");
                if ((*body)["defect_facts"].isObject()) {
                    command.defect_facts = (*body)["defect_facts"];
                }
                const auto outcome = resolution::ImportResolutionService(db_client)
                                         .add_manual_defect(command);
                if (outcome.status != resolution::ResolutionStatus::Ok ||
                    !outcome.manual_defect.has_value()) {
                    const auto error = resolution_error_response(outcome);
                    respond_json(
                        callback,
                        make_error_body(error.error_code, error.error_message),
                        static_cast<drogon::HttpStatusCode>(error.http_status));
                    return;
                }
                auto response = drogon::HttpResponse::newHttpJsonResponse(
                    manual_defect_json(*outcome.manual_defect));
                response->setStatusCode(drogon::k201Created);
                response->addHeader(
                    "ETag",
                    "\"draft-" + std::to_string(outcome.manual_defect->draft_version) + "\"");
                apply_local_dev_cors_headers(response);
                callback(response);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
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
                const auto outcome =
                    resolution::ImportResolutionService(db_client).load_workspace(import_id);
                if (outcome.status != resolution::ResolutionStatus::Ok ||
                    !outcome.workspace.has_value()) {
                    const auto error = resolution_error_response(outcome);
                    respond_json(
                        callback,
                        make_error_body(error.error_code, error.error_message),
                        static_cast<drogon::HttpStatusCode>(error.http_status));
                    return;
                }
                respond_json(callback, resolution_workspace_json(*outcome.workspace));
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});
}

}  // namespace bridge_report::http
