#include "bridge_report/http/DefectMatchingRoutes.hpp"

#include <set>
#include <string>

#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/db/RatingTreeRepository.hpp"
#include "bridge_report/db/ReviewRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"
#include "bridge_report/resolution/ConfirmResolutionReader.hpp"
#include "bridge_report/review/JsonAccessors.hpp"

namespace bridge_report::http {

namespace {

// 页面提交的当前草稿覆盖层：只接受匹配真正用到的字段，且只认已存在的
// candidate_id，客户端无法凭请求体凭空造出新病害。
// 只接受来源事实和校对状态：构件解析与评分树解析都在关系表里，由解析接口写入，
// 客户端再送一份只会让陈旧值盖掉权威状态（5.0 合同也明确拒收这些字段）。
const char* const kOverlayFields[] = {
    "defect_type",
    "defect_description",
    "defect_location",
    "source_defect_group_id",
    "source_defect_group_number",
    "source_defect_indicator_id",
    "source_defect_indicator_number",
    "review_status",
    "group_review_status",
};

void apply_overlay(Json::Value& draft, const Json::Value& body) {
    if (!body.isObject() || !body["defects"].isArray() ||
        !draft["defects"].isArray()) {
        return;
    }
    std::map<std::string, const Json::Value*> overlay;
    for (const auto& item : body["defects"]) {
        if (!item.isObject() || !item["candidate_id"].isString()) continue;
        overlay.emplace(item["candidate_id"].asString(), &item);
    }
    for (auto& defect : draft["defects"]) {
        const auto found =
            overlay.find(review::string_member_or_empty(defect, "candidate_id"));
        if (found == overlay.end()) continue;
        for (const auto* field : kOverlayFields) {
            if (!found->second->isMember(field)) continue;
            const auto& value = (*found->second)[field];
            if (value.isNull()) {
                defect[field] = Json::Value();
            } else if (value.isString()) {
                defect[field] = value.asString();
            }
        }
    }
}

review::DefectMatchScope parse_scope(const Json::Value& body) {
    review::DefectMatchScope scope;
    if (!body.isObject() || !body["candidate_ids"].isArray()) return scope;
    scope.has_scope = true;
    for (const auto& item : body["candidate_ids"]) {
        if (item.isString() && !item.asString().empty()) {
            scope.candidate_ids.insert(item.asString());
        }
    }
    return scope;
}

}  // namespace

Json::Value defect_match_report_json(const review::DefectMatchReport& report) {
    Json::Value body(Json::objectValue);
    auto& summary = body["summary"];
    summary["processed"] = report.stats.processed;
    summary["auto_bound"] = report.stats.auto_bound;
    summary["candidates"] = report.stats.candidates;
    summary["composite"] = report.stats.composite;
    summary["unmatched"] = report.stats.unmatched;
    summary["prerequisite_missing"] = report.stats.prerequisite_missing;
    summary["failed"] = report.stats.failed;
    summary["skipped"] = report.stats.skipped;

    body["results"] = Json::Value(Json::arrayValue);
    for (const auto& record : report.records) {
        Json::Value item(Json::objectValue);
        item["candidate_id"] = record.candidate_id;
        item["outcome"] = rating_tree::to_string(record.outcome);
        item["skipped"] = record.skipped;
        item["rating_tree_node_id"] = record.node_id.has_value()
            ? Json::Value(*record.node_id)
            : Json::Value();
        item["match_method"] = record.match_method.empty()
            ? Json::Value()
            : Json::Value(record.match_method);
        item["match_evidence"] = record.match_evidence.empty()
            ? Json::Value()
            : Json::Value(record.match_evidence);
        item["reason_code"] = record.reason_code.empty()
            ? Json::Value()
            : Json::Value(record.reason_code);
        item["reason_message"] = record.reason_message.empty()
            ? Json::Value()
            : Json::Value(record.reason_message);
        item["candidates"] = Json::Value(Json::arrayValue);
        for (const auto& candidate : record.candidates) {
            Json::Value entry(Json::objectValue);
            entry["rating_tree_node_id"] = candidate.node_id;
            entry["display_name"] = candidate.display_name;
            entry["match_method"] = candidate.match_method;
            entry["evidence"] = candidate.evidence;
            item["candidates"].append(std::move(entry));
        }
        body["results"].append(std::move(item));
    }
    return body;
}

void register_defect_matching_routes(const drogon::orm::DbClientPtr& db_client) {
    const std::string path =
        "/api/import-records/{import_id}/defect-rating-tree-matches";
    register_options_handler(path);

    drogon::app().registerHandler(
        path,
        [db_client](const drogon::HttpRequestPtr& request,
                    HttpCallback&& callback,
                    const std::string& import_record_id) {
            if (!is_valid_uuid(import_record_id)) {
                respond_import_record_not_found(callback);
                return;
            }
            try {
                if (!authenticate_request(db_client, request).has_value()) {
                    respond_unauthorized(callback);
                    return;
                }
                db::ReviewRepository repository(db_client);
                const auto detail =
                    repository.get_import_record_detail(import_record_id);
                if (!detail.has_value()) {
                    respond_import_record_not_found(callback);
                    return;
                }
                if (!detail->rating_tree_version_id.has_value() ||
                    !detail->technical_standard_package_id.has_value()) {
                    respond_json(
                        callback,
                        make_error_body(
                            rating_tree::kReasonRatingTreeNotBound,
                            "本年度尚未锁定评定树，无法执行病害匹配。"),
                        drogon::k409Conflict);
                    return;
                }
                const auto tree = db::RatingTreeRepository(db_client)
                                      .load_published_tree(
                                          *detail->rating_tree_version_id);
                if (!tree.has_value()) {
                    // 评定树装载失败必须显式报错：整批算成"无匹配结果"会
                    // 让用户以为规则没覆盖，从而误改数据。
                    LOG_ERROR << "rating tree catalog unavailable import="
                              << import_record_id << " tree="
                              << *detail->rating_tree_version_id << " reason="
                              << rating_tree::kReasonCatalogUnavailable;
                    respond_json(
                        callback,
                        make_error_body(
                            rating_tree::kReasonCatalogUnavailable,
                            "本年度锁定的评定树当前不可用，请重试或联系管理员。"),
                        drogon::k503ServiceUnavailable);
                    return;
                }

                Json::Value draft =
                    parse_parsed_result_json(detail->parsed_result_json);
                const auto body = request->getJsonObject();
                if (body != nullptr) apply_overlay(draft, *body);
                const auto scope =
                    body == nullptr ? review::DefectMatchScope{} : parse_scope(*body);

                // 构件与评分树解析在关系表里，草稿只剩来源事实（5.0 §7.1）。直接拿草稿
                // 匹配会逐条读到空的 bridge_component_id，把每条病害都判成"尚未绑定
                // 实际构件"。视图把来源事实、构件解析与实例覆盖合到一起，正是匹配要的
                // 输入；页面刚改过的文字仍由上面的 overlay 盖在来源事实上。
                const auto view = resolution::build_confirmable_view(
                    db_client, import_record_id, draft);

                // 年度锁定版本优先，否则该桥最新的**已确认**版本——与绑定写入病害时
                // 用的是同一条规则。此前走 get_latest_revision()（草稿优先），桥上一有
                // 草稿就按草稿的映射挑评定树节点，而这些节点随后会被按已确认版本校验的
                // 保存与入库前检查判为不一致。
                // 匹配只做一件事：按 bridge_component_id 找到构件、取它的生效映射。
                // 因此只装配草稿里真正引用到的那些构件——整份台账在大桥上是 5174 条，
                // 而这里最多几百条，且完整装配还要为每条算 is_referenced（四个 exists
                // 子查询），那部分匹配从不使用。
                std::vector<std::string> referenced_components;
                if (view["defects"].isArray()) {
                    std::set<std::string> unique_ids;
                    for (const auto& defect : view["defects"]) {
                        if (!defect.isObject()) continue;
                        const auto& id = defect["bridge_component_id"];
                        if (id.isString() && !id.asString().empty()) {
                            unique_ids.insert(id.asString());
                        }
                    }
                    referenced_components.assign(unique_ids.begin(), unique_ids.end());
                }
                const auto inventory =
                    db::ComponentInventoryRepository(db_client)
                        .resolve_confirmed_revision_for_components(
                            detail->bridge_id,
                            detail->inspection_year_inventory_revision_id,
                            referenced_components);
                // 只读计算：不写 parsed_result_json，也不碰病害、照片与拆分关系。
                // 自动结果由页面落进本地草稿，保存时服务端再按同一规则复核。
                const auto report = review::match_defect_rating_tree_nodes(
                    view,
                    *detail->technical_standard_package_id,
                    *tree,
                    inventory,
                    scope);
                // 依赖缺失与匹配失败逐条落日志（导入 ID、候选 ID、评定树版本、原因码），
                // 页面只拿到原因码与说明，不暴露内部堆栈。
                for (const auto& record : report.records) {
                    if (record.outcome !=
                            rating_tree::RatingTreeMatchOutcome::prerequisite_missing &&
                        record.outcome !=
                            rating_tree::RatingTreeMatchOutcome::service_error) {
                        continue;
                    }
                    LOG_WARN << "defect rating tree match unresolved import="
                             << import_record_id
                             << " candidate=" << record.candidate_id
                             << " tree=" << *detail->rating_tree_version_id
                             << " reason=" << record.reason_code;
                }
                auto response = defect_match_report_json(report);
                response["rating_tree_version_id"] =
                    *detail->rating_tree_version_id;
                respond_json(callback, response);
            } catch (const std::exception& error) {
                LOG_ERROR << "defect rating tree matching failed import="
                          << import_record_id << " reason="
                          << rating_tree::kReasonMatcherFailed
                          << " detail=" << error.what();
                respond_db_unavailable(callback);
            } catch (...) {
                LOG_ERROR << "defect rating tree matching failed import="
                          << import_record_id << " reason="
                          << rating_tree::kReasonMatcherFailed;
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});
}

}  // namespace bridge_report::http
