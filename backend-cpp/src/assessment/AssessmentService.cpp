#include "bridge_report/assessment/AssessmentService.hpp"

#include "bridge_report/standards/DefectIndicatorResolver.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <utility>

#include "bridge_report/auth/PasswordHash.hpp"
#include "bridge_report/db/ComponentInventoryRepository.hpp"
#include "bridge_report/db/EditLockRepository.hpp"
#include "bridge_report/db/RatingTreeRepository.hpp"
#include "bridge_report/db/StandardRepository.hpp"
#include "bridge_report/standards/TechnicalConditionStandard.hpp"

namespace bridge_report::assessment {
namespace {

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

std::string string_member(const Json::Value& value, const char* key) {
    return value.isObject() && value[key].isString() ? value[key].asString() : std::string();
}

Json::Value defect_result_json(const standards::DefectAssessmentResult& result) {
    Json::Value json;
    json["defect_indicator_id"] = result.defect_indicator_id;
    json["scale"] = result.scale;
    json["deduction"] = result.deduction;
    json["deduction_rule_id"] = result.deduction_rule_id;
    json["source_reference"] = result.source_reference;
    return json;
}

Json::Value component_result_json(const standards::ComponentAssessmentResult& result) {
    Json::Value json;
    json["component_instance_id"] = result.component_instance_id;
    json["component_type_id"] = result.component_type_id;
    json["structure_part"] = standards::to_string(result.structure_part);
    json["major"] = result.major;
    json["score"] = result.score;
    json["ordered_deductions"] = Json::Value(Json::arrayValue);
    for (const auto deduction : result.ordered_deductions) {
        json["ordered_deductions"].append(deduction);
    }
    json["defects"] = Json::Value(Json::arrayValue);
    for (const auto& defect : result.defects) {
        json["defects"].append(defect_result_json(defect));
    }
    return json;
}

Json::Value result_json_impl(const standards::BridgeAssessmentResult& result) {
    Json::Value json;
    json["standard_id"] = result.standard_id;
    json["package_version"] = result.package_version;
    json["bridge_type_id"] = result.bridge_type_id;
    json["overall_score"] = result.overall_score;
    json["calculated_grade"] = result.calculated_grade;
    json["final_grade"] = result.final_grade;
    json["explanation"] = result.explanation;
    json["structure_parts"] = Json::Value(Json::arrayValue);
    for (const auto& part : result.structure_parts) {
        Json::Value part_json;
        part_json["structure_part"] = standards::to_string(part.structure_part);
        part_json["score"] = part.score;
        part_json["grade"] = part.grade;
        part_json["overall_weight"] = part.overall_weight;
        part_json["categories"] = Json::Value(Json::arrayValue);
        for (const auto& category : part.categories) {
            Json::Value category_json;
            category_json["component_type_id"] = category.component_type_id;
            category_json["structure_part"] = standards::to_string(category.structure_part);
            category_json["major"] = category.major;
            category_json["score"] = category.score;
            category_json["grade"] = category.grade;
            category_json["mean_component_score"] = category.mean_component_score;
            category_json["minimum_component_score"] = category.minimum_component_score;
            category_json["configured_weight"] = category.configured_weight;
            category_json["effective_weight"] = category.effective_weight;
            category_json["low_score_passthrough"] = category.low_score_passthrough;
            category_json["component_count_factor"] = category.component_count_factor.has_value()
                ? Json::Value(*category.component_count_factor)
                : Json::Value(Json::nullValue);
            category_json["components"] = Json::Value(Json::arrayValue);
            for (const auto& component : category.components) {
                category_json["components"].append(component_result_json(component));
            }
            part_json["categories"].append(category_json);
        }
        json["structure_parts"].append(part_json);
    }
    json["triggered_controls"] = Json::Value(Json::arrayValue);
    for (const auto& control : result.triggered_controls) {
        Json::Value control_json;
        control_json["control_id"] = control.control_id;
        control_json["source_reference"] = control.source_reference;
        control_json["label"] = control.label;
        control_json["result_grade"] = control.result_grade.has_value()
            ? Json::Value(*control.result_grade)
            : Json::Value(Json::nullValue);
        json["triggered_controls"].append(control_json);
    }
    json["trace"] = Json::Value(Json::arrayValue);
    for (const auto& trace : result.trace) {
        Json::Value trace_json;
        trace_json["step"] = trace.step;
        trace_json["rule_id"] = trace.rule_id;
        trace_json["entity_id"] = trace.entity_id;
        trace_json["source_reference"] = trace.source_reference;
        trace_json["inputs"] = trace.inputs;
        trace_json["output"] = trace.output;
        json["trace"].append(trace_json);
    }
    return json;
}

AssessmentPreviewIssue issue(
    std::string code,
    std::string message,
    std::string entity_type = "assessment",
    std::string entity_id = {},
    std::string field_path = {},
    std::string rule_id = {}) {
    return {std::move(code), std::move(message), std::move(entity_type),
            std::move(entity_id), std::move(field_path), std::move(rule_id)};
}

Json::Value standard_identity_json(const standards::StandardManifest& manifest) {
    Json::Value identity;
    identity["standard_id"] = manifest.standard_id;
    identity["standard_code"] = manifest.standard_code;
    identity["standard_name"] = manifest.standard_name;
    identity["official_edition"] = manifest.official_edition;
    identity["package_version"] = manifest.package_version;
    identity["content_checksum"] = manifest.content_checksum;
    identity["algorithm_id"] = manifest.algorithm_id;
    return identity;
}

std::string lock_issue_code(db::EditLockCheckStatus status) {
    switch (status) {
        case db::EditLockCheckStatus::invalid: return "edit_lock_invalid";
        case db::EditLockCheckStatus::expired: return "edit_lock_expired";
        case db::EditLockCheckStatus::force_released: return "edit_lock_force_released";
        case db::EditLockCheckStatus::required: return "edit_lock_required";
        case db::EditLockCheckStatus::active: break;
    }
    return {};
}

bool contains_text(
    const std::vector<std::string>& values,
    const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool rating_tree_node_applies(
    const rating_tree::EffectiveRatingTreeNode& node,
    const std::string& bridge_type_id,
    const std::string& component_category_id) {
    return node.node_type == rating_tree::RatingTreeNodeType::defect &&
        node.is_selectable &&
        contains_text(node.bridge_type_ids, bridge_type_id) &&
        contains_text(
            node.component_category_ids, component_category_id);
}

}  // namespace

Json::Value AssessmentPreviewIssue::to_json() const {
    Json::Value json;
    json["code"] = code;
    json["message"] = message;
    json["entity_type"] = entity_type;
    json["entity_id"] = entity_id;
    json["field_path"] = field_path;
    json["rule_id"] = rule_id;
    return json;
}

Json::Value AssessmentPreview::to_json() const {
    Json::Value json;
    json["client_revision"] = client_revision;
    json["input_checksum"] = input_checksum;
    json["input_summary"] = input_summary;
    json["standard"] = standard_identity;
    json["result"] = result.has_value() ? assessment_result_to_json(*result) : Json::Value(Json::nullValue);
    json["issues"] = Json::Value(Json::arrayValue);
    for (const auto& item : issues) json["issues"].append(item.to_json());
    json["assessment_run_id"] = assessment_run_id.has_value()
        ? Json::Value(*assessment_run_id)
        : Json::Value(Json::nullValue);
    return json;
}

Json::Value assessment_result_to_json(
    const standards::BridgeAssessmentResult& result) {
    return result_json_impl(result);
}

AssessmentPreview calculate_assessment_preview(
    const standards::TechnicalConditionStandard& evaluator,
    const standards::StandardPackage& package,
    const AssessmentContextSnapshot& context,
    const Json::Value& draft,
    int client_revision) {
    AssessmentPreview preview;
    preview.client_revision = client_revision;
    preview.standard_identity = standard_identity_json(package.manifest);
    if (context.rating_tree.has_value()) {
        Json::Value tree_identity;
        tree_identity["version_id"] = context.rating_tree_version_id;
        tree_identity["tree_code"] =
            context.rating_tree->version.tree_code;
        tree_identity["tree_name"] =
            context.rating_tree->version.tree_name;
        tree_identity["package_version"] =
            context.rating_tree->version.package_version;
        tree_identity["content_checksum"] =
            context.rating_tree_content_checksum;
        preview.standard_identity["rating_tree"] =
            std::move(tree_identity);
    }
    preview.issues = context.issues;

    Json::Value summary;
    summary["bridge_type_id"] = context.bridge_type_id;
    summary["inventory_revision_id"] = context.inventory_revision_id;
    summary["rating_tree_version_id"] =
        context.rating_tree_version_id;
    summary["rating_tree_content_checksum"] =
        context.rating_tree_content_checksum;
    summary["components"] = Json::Value(Json::arrayValue);
    summary["defects"] = Json::Value(Json::arrayValue);
    summary["rating_tree_skips"] = Json::Value(Json::arrayValue);

    if (!context.inventory_confirmed) {
        preview.issues.push_back(issue(
            "assessment_inventory_not_confirmed", "构件台账尚未确认，不能进行系统评定。",
            "component_inventory", context.inventory_revision_id, "status"));
    }
    if (!context.rating_tree.has_value() ||
        context.rating_tree_version_id.empty() ||
        context.rating_tree_content_checksum.empty()) {
        preview.issues.push_back(issue(
            "assessment_rating_tree_required",
            "检测年度尚未锁定可用的评定树，不能进行系统评定。",
            "inspection_year",
            context.inspection_year_id,
            "rating_tree_version_id"));
    }

    std::map<std::string, std::string> component_types;
    std::vector<AssessmentComponentSnapshot> components = context.components;
    std::sort(components.begin(), components.end(), [](const auto& left, const auto& right) {
        return std::tie(left.component_instance_id, left.component_type_id) <
               std::tie(right.component_instance_id, right.component_type_id);
    });
    standards::BridgeAssessmentInput input;
    input.bridge_type_id = context.bridge_type_id;
    for (const auto& component : components) {
        component_types[component.component_instance_id] = component.component_type_id;
        input.components.push_back({component.component_instance_id, component.component_type_id, {}});
        Json::Value component_json;
        component_json["component_instance_id"] = component.component_instance_id;
        component_json["component_type_id"] = component.component_type_id;
        summary["components"].append(component_json);
    }

    std::map<std::pair<std::string, std::string>, int> aggregated_scales;
    if (!draft["defects"].isArray()) {
        preview.issues.push_back(issue(
            "assessment_defects_invalid", "草稿中的病害列表不是数组。", "draft", {}, "defects"));
    } else {
        for (const auto& defect : draft["defects"]) {
            if (string_member(defect, "review_status") == "已忽略") continue;
            const auto candidate_id = string_member(defect, "candidate_id");
            const auto component_id = string_member(defect, "bridge_component_id");
            const auto client_category = string_member(defect, "standard_component_category_id");
            const auto component = component_types.find(component_id);
            if (component == component_types.end() ||
                (!client_category.empty() && client_category != component->second)) {
                preview.issues.push_back(issue(
                    "assessment_defect_component_unmatched", "病害未关联到当前已确认台账中的规范构件。",
                    "defect", candidate_id, "bridge_component_id"));
                continue;
            }
            const auto defect_tree_version =
                string_member(defect, "rating_tree_version_id");
            const auto node_id =
                string_member(defect, "rating_tree_node_id");
            const rating_tree::EffectiveRatingTreeNode* node = nullptr;
            if (context.rating_tree.has_value()) {
                const auto found =
                    context.rating_tree->nodes.find(node_id);
                if (found != context.rating_tree->nodes.end()) {
                    node = &found->second;
                }
            }
            if (defect_tree_version != context.rating_tree_version_id ||
                node == nullptr) {
                preview.issues.push_back(issue(
                    "assessment_rating_tree_node_required",
                    "病害尚未选择当前年度评定树中的有效节点。",
                    "defect",
                    candidate_id,
                    "rating_tree_node_id"));
                continue;
            }
            if (!rating_tree_node_applies(
                    *node,
                    context.bridge_type_id,
                    component->second)) {
                preview.issues.push_back(issue(
                    "assessment_rating_tree_node_not_applicable",
                    "评定树节点不适用于当前实际构件。",
                    "defect",
                    candidate_id,
                    "rating_tree_node_id"));
                continue;
            }
            Json::Value defect_json;
            defect_json["candidate_id"] = candidate_id;
            defect_json["component_instance_id"] = component_id;
            defect_json["component_type_id"] = component->second;
            defect_json["rating_tree_node_id"] = node_id;
            defect_json["rating_tree_scoring_mode"] =
                rating_tree::to_string(node->scoring_mode);
            if (node->scoring_mode ==
                rating_tree::RatingTreeScoringMode::non_scoring) {
                defect_json["skipped"] = true;
                defect_json["skip_reason"] =
                    "rating_tree_non_scoring";
                summary["defects"].append(defect_json);
                Json::Value skip;
                skip["candidate_id"] = candidate_id;
                skip["rating_tree_node_id"] = node_id;
                skip["reason"] = "rating_tree_non_scoring";
                summary["rating_tree_skips"].append(std::move(skip));
                continue;
            }
            const auto indicator_id =
                node->h21_indicator_id.value_or("");
            if (indicator_id.empty() ||
                string_member(
                    defect,
                    "standard_defect_indicator_id") != indicator_id) {
                preview.issues.push_back(issue(
                    "assessment_rating_tree_indicator_mismatch",
                    "病害的 H21 评分来源与评定树节点解析结果不一致。",
                    "defect",
                    candidate_id,
                    "standard_defect_indicator_id"));
                continue;
            }
            if (!defect["defect_scale"].isInt() || defect["defect_scale"].asInt() <= 0) {
                preview.issues.push_back(issue(
                    "assessment_defect_scale_required", "病害缺少有效的规范标度。",
                    "defect", candidate_id, "defect_scale"));
                continue;
            }
            const auto scale = defect["defect_scale"].asInt();
            if (std::find(
                    node->allowed_scales.begin(),
                    node->allowed_scales.end(),
                    scale) == node->allowed_scales.end()) {
                preview.issues.push_back(issue(
                    "assessment_rating_tree_scale_not_allowed",
                    "病害标度不在评定树节点允许范围内。",
                    "defect",
                    candidate_id,
                    "defect_scale"));
                continue;
            }
            const auto resolution = standards::resolve_defect_indicator(
                package, indicator_id, component->second, scale);
            if (!resolution.ok()) {
                switch (resolution.status) {
                    case standards::DefectIndicatorResolutionStatus::indicator_required:
                        preview.issues.push_back(issue(
                            "assessment_defect_indicator_required",
                            "病害尚未选择当前规范中的病害指标。",
                            "defect", candidate_id,
                            "standard_defect_indicator_id"));
                        break;
                    case standards::DefectIndicatorResolutionStatus::indicator_unknown:
                        preview.issues.push_back(issue(
                            "assessment_defect_indicator_unknown",
                            "病害指标不属于当前锁定的规范包。",
                            "defect", candidate_id,
                            "standard_defect_indicator_id"));
                        break;
                    case standards::DefectIndicatorResolutionStatus::indicator_not_applicable:
                        preview.issues.push_back(issue(
                            "assessment_defect_indicator_not_applicable",
                            "病害指标不适用于当前实际构件类别。",
                            "defect", candidate_id,
                            "standard_defect_indicator_id"));
                        break;
                    case standards::DefectIndicatorResolutionStatus::scale_not_allowed:
                        preview.issues.push_back(issue(
                            "assessment_defect_scale_not_allowed",
                            "病害标度不在该规范指标允许范围内。",
                            "defect", candidate_id, "defect_scale"));
                        break;
                    case standards::DefectIndicatorResolutionStatus::resolved:
                        break;
                }
                continue;
            }
            auto& aggregated = aggregated_scales[{component_id, indicator_id}];
            aggregated = (std::max)(aggregated, scale);
            defect_json["defect_indicator_id"] = indicator_id;
            defect_json["defect_indicator_name"] = resolution.indicator_name;
            defect_json["scale"] = scale;
            summary["defects"].append(defect_json);
        }
    }

    preview.input_summary = summary;
    preview.input_checksum = "sha256:" + auth::sha256_hex(compact_json(summary));
    if (!preview.issues.empty()) return preview;

    for (auto& component : input.components) {
        for (const auto& [key, scale] : aggregated_scales) {
            if (key.first == component.component_instance_id) {
                component.defects.push_back({key.second, scale});
            }
        }
    }

    auto outcome = evaluator.evaluate(input);
    for (const auto& item : outcome.issues) {
        preview.issues.push_back(issue(
            item.code, item.message, "assessment", item.entity_id, {}, item.rule_id));
    }
    if (preview.issues.empty() && outcome.result.has_value()) {
        preview.result = std::move(outcome.result);
    }
    return preview;
}

AssessmentService::AssessmentService(
    drogon::orm::DbClientPtr db_client,
    std::shared_ptr<const standards::StandardRegistry> registry)
    : db_client_(std::move(db_client)), registry_(std::move(registry)) {}

AssessmentServiceOutcome AssessmentService::preview(
    const std::string& import_record_id,
    const AssessmentPreviewPayload& payload,
    const db::AuthUser& user,
    const std::string& edit_lock_token) const {
    AssessmentServiceOutcome outcome;
    outcome.preview.client_revision = payload.client_revision;
    const auto lock = db::EditLockRepository(db_client_).check(
        import_record_id, user, edit_lock_token);
    if (!lock.active()) {
        outcome.status = AssessmentServiceStatus::LockRejected;
        outcome.preview.issues.push_back(issue(
            lock_issue_code(lock.status), "当前编辑锁无效，不能提交未保存草稿试算。",
            "import_record", import_record_id, "edit_lock"));
        return outcome;
    }

    const auto rows = db_client_->execSqlSync(
        "select ir.inspection_year_id::text as inspection_year_id,"
        "iy.standard_profile_id::text as standard_profile_id,"
        "iy.component_inventory_revision_id::text as inventory_revision_id,"
        "p.technical_condition_package_id::text as package_id,"
        "p.rating_tree_version_id::text as rating_tree_version_id,"
        "rtv.tree_content_checksum as rating_tree_content_checksum "
        "from import_records ir "
        "left join inspection_years iy on iy.id=ir.inspection_year_id "
        "left join project_standard_profiles p on p.id=iy.standard_profile_id "
        "left join rating_tree_versions rtv on rtv.id=p.rating_tree_version_id "
        "where ir.id=$1::uuid",
        import_record_id);
    if (rows.empty()) {
        outcome.status = AssessmentServiceStatus::NotFound;
        return outcome;
    }
    const auto& row = rows[0];
    if (row["inspection_year_id"].isNull() || row["standard_profile_id"].isNull() ||
        row["inventory_revision_id"].isNull() || row["package_id"].isNull() ||
        row["rating_tree_version_id"].isNull() ||
        row["rating_tree_content_checksum"].isNull()) {
        outcome.status = AssessmentServiceStatus::Blocked;
        outcome.preview.issues.push_back(issue(
            "assessment_context_incomplete", "检测年度尚未配置规范组合和构件台账。",
            "inspection_year", {}, "standard_profile_id"));
        return outcome;
    }

    AssessmentContextSnapshot context;
    context.inspection_year_id = row["inspection_year_id"].as<std::string>();
    context.standard_profile_id = row["standard_profile_id"].as<std::string>();
    context.inventory_revision_id = row["inventory_revision_id"].as<std::string>();
    context.standard_package_id = row["package_id"].as<std::string>();
    context.rating_tree_version_id =
        row["rating_tree_version_id"].as<std::string>();
    context.rating_tree_content_checksum =
        row["rating_tree_content_checksum"].as<std::string>();
    context.rating_tree =
        db::RatingTreeRepository(db_client_).load_published_tree(
            context.rating_tree_version_id);
    if (!context.rating_tree.has_value() ||
        context.rating_tree->version.tree_content_checksum !=
            context.rating_tree_content_checksum) {
        outcome.status = AssessmentServiceStatus::Blocked;
        outcome.preview.issues.push_back(issue(
            "assessment_rating_tree_unavailable",
            "项目锁定的评定树未发布或内容校验失败。",
            "rating_tree",
            context.rating_tree_version_id));
        return outcome;
    }

    db::StandardRepository standard_repository(db_client_);
    const auto package_record = standard_repository.find_package_by_id(context.standard_package_id);
    if (!package_record.has_value()) {
        outcome.status = AssessmentServiceStatus::Blocked;
        outcome.preview.issues.push_back(issue(
            "assessment_standard_unavailable", "项目锁定的技术状况评定规范不存在。",
            "standard_package", context.standard_package_id));
        return outcome;
    }
    const standards::StandardPackageKey key{
        package_record->family, package_record->standard_id, package_record->package_version};
    const auto* package = registry_ != nullptr ? registry_->find(key) : nullptr;
    if (package == nullptr || package->manifest.content_checksum != package_record->content_checksum ||
        !package_record->is_enabled || package_record->sync_status != "正常") {
        outcome.status = AssessmentServiceStatus::Blocked;
        outcome.preview.issues.push_back(issue(
            "assessment_standard_unavailable", "项目锁定的规则包未启用、故障或内容校验失败。",
            "standard_package", context.standard_package_id));
        return outcome;
    }
    auto algorithm = registry_->create_algorithm(key);
    const auto* evaluator = dynamic_cast<const standards::TechnicalConditionStandard*>(algorithm.get());
    if (evaluator == nullptr) {
        outcome.status = AssessmentServiceStatus::Blocked;
        outcome.preview.issues.push_back(issue(
            "assessment_algorithm_unavailable", "项目锁定的评分规范没有可用的技术状况评定适配器。",
            "standard_package", context.standard_package_id));
        return outcome;
    }

    db::ComponentInventoryRepository inventory_repository(db_client_);
    const auto inventory = inventory_repository.get_revision(context.inventory_revision_id);
    if (!inventory.has_value()) {
        outcome.status = AssessmentServiceStatus::Blocked;
        outcome.preview.issues.push_back(issue(
            "assessment_inventory_missing", "项目锁定的构件台账不存在。",
            "component_inventory", context.inventory_revision_id));
        return outcome;
    }
    context.inventory_confirmed = inventory->status == "已确认";
    for (const auto& entry : inventory->entries) {
        if (!entry.is_active) continue;
        const auto mapping = std::find_if(entry.mappings.begin(), entry.mappings.end(), [&](const auto& item) {
            return item.is_active && item.confirmation_status == "已确认" &&
                   item.standard_package_id == context.standard_package_id;
        });
        if (mapping == entry.mappings.end()) {
            context.issues.push_back(issue(
                "assessment_component_mapping_required", "构件缺少当前规范的已确认类别映射。",
                "component", entry.bridge_component_id, "standard_component_category_id"));
            continue;
        }
        if (context.bridge_type_id.empty()) context.bridge_type_id = mapping->standard_bridge_type_id;
        if (context.bridge_type_id != mapping->standard_bridge_type_id) {
            context.issues.push_back(issue(
                "assessment_bridge_type_inconsistent", "构件台账包含不一致的规范桥型映射。",
                "component", entry.bridge_component_id, "standard_bridge_type_id"));
            continue;
        }
        context.components.push_back({entry.bridge_component_id, mapping->standard_component_category_id});
    }

    outcome.preview = calculate_assessment_preview(*evaluator, *package, context, payload.draft, payload.client_revision);
    outcome.status = outcome.preview.result.has_value()
        ? AssessmentServiceStatus::Completed
        : AssessmentServiceStatus::Blocked;

    const auto response_json = outcome.preview.to_json();
    Json::Value result_summary;
    result_summary["client_revision"] = payload.client_revision;
    result_summary["result"] = response_json["result"];
    result_summary["issues"] = response_json["issues"];
    db_client_->execSqlSync(
        "delete from assessment_runs where source_import_record_id=$1::uuid and run_kind='试算'",
        import_record_id);
    const auto inserted = db_client_->execSqlSync(
        "insert into assessment_runs(inspection_year_id,source_import_record_id,run_kind,"
        "technical_condition_package_id,standard_profile_id,component_inventory_revision_id,"
        "rating_tree_version_id,rating_tree_content_checksum,"
        "result_status,input_summary_json,input_checksum,rule_package_summary_json,"
        "rule_package_checksum,result_summary_json,created_by_user_id) "
        "values($1::uuid,$2::uuid,'试算',$3::uuid,$4::uuid,$5::uuid,$6::uuid,$7,"
        "$8,$9::jsonb,$10,$11::jsonb,$12,$13::jsonb,$14::uuid) returning id::text",
        context.inspection_year_id, import_record_id, context.standard_package_id,
        context.standard_profile_id, context.inventory_revision_id,
        context.rating_tree_version_id, context.rating_tree_content_checksum,
        outcome.preview.result.has_value() ? "成功" : "阻断",
        compact_json(outcome.preview.input_summary), outcome.preview.input_checksum,
        compact_json(outcome.preview.standard_identity), package->manifest.content_checksum,
        compact_json(result_summary), user.id);
    outcome.preview.assessment_run_id = inserted[0]["id"].as<std::string>();
    return outcome;
}

}  // namespace bridge_report::assessment
