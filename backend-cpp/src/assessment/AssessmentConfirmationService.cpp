#include "bridge_report/assessment/AssessmentConfirmationService.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "bridge_report/db/RatingTreeRepository.hpp"

namespace bridge_report::assessment {
namespace {

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

AssessmentPreviewIssue issue(
    std::string code,
    std::string message,
    std::string entity_type,
    std::string entity_id = {},
    std::string field_path = {}) {
    return {std::move(code), std::move(message), std::move(entity_type),
            std::move(entity_id), std::move(field_path), {}};
}

std::string chinese_structure_part(standards::StructurePart part) {
    switch (part) {
        case standards::StructurePart::superstructure: return "上部结构";
        case standards::StructurePart::substructure: return "下部结构";
        case standards::StructurePart::deck_system: return "桥面系";
    }
    return "其他";
}

std::string trace_target_type(const std::string& step) {
    if (step == "defect_deduction") return "病害";
    if (step == "component_score") return "构件";
    if (step == "component_category_score") return "部件";
    if (step == "structure_part_score") return "结构";
    if (step == "grade") return "等级";
    if (step == "control") return "控制";
    return "全桥";
}

Json::Value component_json(const standards::ComponentAssessmentResult& component) {
    Json::Value json;
    json["component_instance_id"] = component.component_instance_id;
    json["component_type_id"] = component.component_type_id;
    json["structure_part"] = standards::to_string(component.structure_part);
    json["major"] = component.major;
    json["score"] = component.score;
    json["ordered_deductions"] = Json::Value(Json::arrayValue);
    for (const auto deduction : component.ordered_deductions) {
        json["ordered_deductions"].append(deduction);
    }
    json["defects"] = Json::Value(Json::arrayValue);
    for (const auto& defect : component.defects) {
        Json::Value value;
        value["defect_indicator_id"] = defect.defect_indicator_id;
        value["scale"] = defect.scale;
        value["deduction"] = defect.deduction;
        value["deduction_rule_id"] = defect.deduction_rule_id;
        value["source_reference"] = defect.source_reference;
        json["defects"].append(value);
    }
    return json;
}

Json::Value category_json(const standards::ComponentCategoryAssessmentResult& category) {
    Json::Value json;
    json["component_type_id"] = category.component_type_id;
    json["component_type_name"] = category.component_type_name;
    json["structure_part"] = standards::to_string(category.structure_part);
    json["major"] = category.major;
    json["score"] = category.score;
    json["grade"] = category.grade;
    json["mean_component_score"] = category.mean_component_score;
    json["minimum_component_score"] = category.minimum_component_score;
    json["configured_weight"] = category.configured_weight;
    json["effective_weight"] = category.effective_weight;
    json["low_score_passthrough"] = category.low_score_passthrough;
    json["component_count_factor"] = category.component_count_factor.has_value()
        ? Json::Value(*category.component_count_factor)
        : Json::Value(Json::nullValue);
    return json;
}

}  // namespace

AssessmentConfirmationCalculation calculate_assessment_confirmation(
    const standards::TechnicalConditionStandard& evaluator,
    const standards::StandardPackage& package,
    const AssessmentContextSnapshot& context,
    const Json::Value& draft) {
    AssessmentConfirmationCalculation calculation;
    calculation.preview = calculate_assessment_preview(
        evaluator, package, context, draft, 0);
    calculation.issues = calculation.preview.issues;
    return calculation;
}

AssessmentConfirmationService::AssessmentConfirmationService(
    drogon::orm::DbClientPtr transaction,
    std::shared_ptr<const standards::StandardRegistry> registry)
    : transaction_(std::move(transaction)), registry_(std::move(registry)) {}

AssessmentConfirmationOutcome AssessmentConfirmationService::calculate(
    const std::string& inspection_year_id,
    const Json::Value& draft,
    const std::optional<std::string>& inventory_revision_override) const {
    AssessmentConfirmationOutcome outcome;
    if (registry_ == nullptr) {
        outcome.preview.issues.push_back(issue(
            "assessment_registry_unavailable", "系统评分规范注册表不可用。",
            "standard_registry"));
        return outcome;
    }

    // override 单独校验，不混进下面那条大查询：那条用内连接，任何不满足都只会塌成
    // "上下文不完整"，分不出"年度没锁版本"与"给的版本不合法"。
    if (inventory_revision_override.has_value()) {
        const auto guard = transaction_->execSqlSync(
            "select iy.component_inventory_revision_id::text as locked_revision_id,"
            "exists(select 1 from bridge_component_inventory_revisions r "
            "where r.id=$2::uuid and r.bridge_id=iy.bridge_id "
            "and r.status in ('已确认','confirmed')) as override_usable "
            "from inspection_years iy where iy.id=$1::uuid",
            inspection_year_id, *inventory_revision_override);
        if (guard.empty()) {
            outcome.preview.issues.push_back(issue(
                "assessment_context_incomplete", "检测年度不存在。",
                "inspection_year", inspection_year_id, "id"));
            return outcome;
        }
        if (!guard[0]["override_usable"].as<bool>()) {
            outcome.preview.issues.push_back(issue(
                "assessment_context_incomplete",
                "指定的构件台账版本不属于本桥梁或尚未确认。",
                "inspection_year", inspection_year_id,
                "component_inventory_revision_id"));
            return outcome;
        }
        // 年度已经锁了别的版本：既不能静默改用 override，也不能静默忽略它。
        if (!guard[0]["locked_revision_id"].isNull()
            && guard[0]["locked_revision_id"].as<std::string>()
                   != *inventory_revision_override) {
            outcome.preview.issues.push_back(issue(
                "component_inventory_revision_changed",
                "检测年度锁定的构件台账版本与本次请求依据的版本不一致。",
                "inspection_year", inspection_year_id,
                "component_inventory_revision_id"));
            return outcome;
        }
    }

    const auto context_rows = transaction_->execSqlSync(
        "select iy.id::text as inspection_year_id,iy.standard_profile_id::text as profile_id,"
        "coalesce(nullif($2,'')::uuid,iy.component_inventory_revision_id)::text "
        "as inventory_revision_id,"
        "p.technical_condition_package_id::text as package_id,"
        "p.rating_tree_version_id::text as rating_tree_version_id,"
        "rtv.tree_content_checksum as rating_tree_content_checksum,"
        "sp.standard_family,sp.standard_id,sp.package_version,sp.content_checksum,"
        "sp.is_enabled,sp.sync_status,ir.bridge_id::text as bridge_id "
        "from inspection_years iy "
        "join import_records ir on ir.inspection_year_id=iy.id "
        "join project_standard_profiles p on p.id=iy.standard_profile_id "
        "join rating_tree_versions rtv on rtv.id=p.rating_tree_version_id "
        "and rtv.status='published' "
        "join standard_packages sp on sp.id=p.technical_condition_package_id "
        "join bridge_component_inventory_revisions r "
        "on r.id=coalesce(nullif($2,'')::uuid,iy.component_inventory_revision_id) "
        "and r.bridge_id=iy.bridge_id "
        "where iy.id=$1::uuid limit 1 for update of iy,p,sp,r",
        inspection_year_id, inventory_revision_override.value_or(std::string()));
    if (context_rows.empty()) {
        outcome.preview.issues.push_back(issue(
            "assessment_context_incomplete",
            "检测年度尚未锁定规范组合和已确认构件台账。",
            "inspection_year", inspection_year_id, "standard_profile_id"));
        return outcome;
    }

    const auto& row = context_rows[0];
    const auto family = standards::parse_standard_family(
        row["standard_family"].as<std::string>());
    if (!family.has_value()) {
        outcome.preview.issues.push_back(issue(
            "assessment_standard_unavailable", "项目锁定的技术状况评定规范类型无效。",
            "standard_package", row["package_id"].as<std::string>()));
        return outcome;
    }
    const standards::StandardPackageKey key{
        *family,
        row["standard_id"].as<std::string>(),
        row["package_version"].as<std::string>()};
    const auto* package = registry_->find(key);
    if (package == nullptr || !row["is_enabled"].as<bool>() ||
        row["sync_status"].as<std::string>() != "正常" ||
        package->manifest.content_checksum != row["content_checksum"].as<std::string>()) {
        outcome.preview.issues.push_back(issue(
            "assessment_standard_unavailable",
            "项目锁定的规则包未启用、故障或内容校验失败。",
            "standard_package", row["package_id"].as<std::string>()));
        return outcome;
    }
    auto algorithm = registry_->create_algorithm(key);
    const auto* evaluator =
        dynamic_cast<const standards::TechnicalConditionStandard*>(algorithm.get());
    if (evaluator == nullptr) {
        outcome.preview.issues.push_back(issue(
            "assessment_algorithm_unavailable",
            "项目锁定的评分规范没有可用的技术状况评定适配器。",
            "standard_package", row["package_id"].as<std::string>()));
        return outcome;
    }

    AssessmentContextSnapshot context;
    context.inspection_year_id = inspection_year_id;
    context.standard_profile_id = row["profile_id"].as<std::string>();
    context.inventory_revision_id = row["inventory_revision_id"].as<std::string>();
    context.standard_package_id = row["package_id"].as<std::string>();
    context.rating_tree_version_id =
        row["rating_tree_version_id"].as<std::string>();
    context.rating_tree_content_checksum =
        row["rating_tree_content_checksum"].as<std::string>();
    context.rating_tree =
        db::RatingTreeRepository(transaction_).load_published_tree(
            context.rating_tree_version_id);
    if (!context.rating_tree.has_value() ||
        context.rating_tree->version.tree_content_checksum !=
            context.rating_tree_content_checksum) {
        outcome.preview.issues.push_back(issue(
            "assessment_rating_tree_unavailable",
            "项目锁定的评定树未发布或内容校验失败。",
            "rating_tree",
            context.rating_tree_version_id));
        return outcome;
    }

    const auto inventory_rows = transaction_->execSqlSync(
        "select r.status,e.bridge_component_id::text as component_id,"
        "m.standard_bridge_type_id,m.standard_component_category_id "
        "from bridge_component_inventory_revisions r "
        "join bridge_component_inventory_entries e "
        "on e.inventory_revision_id=r.id and e.is_active "
        "left join bridge_component_standard_mappings m "
        "on m.inventory_entry_id=e.id and m.standard_package_id=$2::uuid "
        "and m.is_active and m.confirmation_status='已确认' "
        "where r.id=$1::uuid order by e.sort_order,e.id",
        context.inventory_revision_id, context.standard_package_id);
    context.inventory_confirmed = !inventory_rows.empty() &&
        inventory_rows[0]["status"].as<std::string>() == "已确认";
    for (const auto& inventory_row : inventory_rows) {
        const auto component_id = inventory_row["component_id"].as<std::string>();
        if (inventory_row["standard_component_category_id"].isNull() ||
            inventory_row["standard_bridge_type_id"].isNull()) {
            context.issues.push_back(issue(
                "assessment_component_mapping_required",
                "构件缺少当前规范的已确认类别映射。",
                "component", component_id,
                "standard_component_category_id"));
            continue;
        }
        const auto bridge_type =
            inventory_row["standard_bridge_type_id"].as<std::string>();
        if (context.bridge_type_id.empty()) context.bridge_type_id = bridge_type;
        if (context.bridge_type_id != bridge_type) {
            context.issues.push_back(issue(
                "assessment_bridge_type_inconsistent",
                "构件台账包含不一致的规范桥型映射。",
                "component", component_id, "standard_bridge_type_id"));
            continue;
        }
        context.components.push_back({
            component_id,
            inventory_row["standard_component_category_id"].as<std::string>()});
    }

    const auto calculation = calculate_assessment_confirmation(
        *evaluator, *package, context, draft);
    outcome.preview = calculation.preview;
    outcome.status = outcome.preview.result.has_value()
        ? AssessmentConfirmationStatus::Completed
        : AssessmentConfirmationStatus::Blocked;
    return outcome;
}

AssessmentConfirmationWritten AssessmentConfirmationService::persist(
    const AssessmentPreview& preview,
    const std::string& target_inspection_year_id,
    const std::string& source_import_record_id,
    const std::string& confirmed_by_user_id) const {
    if (!preview.result.has_value()) {
        throw std::invalid_argument("cannot persist a blocked formal assessment");
    }
    const auto& result = *preview.result;
    const auto context = transaction_->execSqlSync(
        "select iy.standard_profile_id::text as profile_id,"
        "iy.component_inventory_revision_id::text as inventory_revision_id,"
        "p.technical_condition_package_id::text as package_id,"
        "p.rating_tree_version_id::text as rating_tree_version_id,"
        "tree.tree_content_checksum as rating_tree_content_checksum "
        "from inspection_years iy join project_standard_profiles p "
        "on p.id=iy.standard_profile_id "
        "join rating_tree_versions tree on tree.id=p.rating_tree_version_id "
        "where iy.id=$1::uuid for update of iy,p",
        target_inspection_year_id);
    if (context.empty()) {
        throw std::runtime_error("formal assessment target context is incomplete");
    }

    const auto existing = transaction_->execSqlSync(
        "select id::text as id,formal_revision_number,is_current "
        "from assessment_runs where inspection_year_id=$1::uuid and run_kind='正式' "
        "order by formal_revision_number for update",
        target_inspection_year_id);
    int formal_revision = 1;
    std::string supersedes_run_id;
    for (const auto& row : existing) {
        formal_revision = (std::max)(
            formal_revision, row["formal_revision_number"].as<int>() + 1);
        if (row["is_current"].as<bool>()) {
            supersedes_run_id = row["id"].as<std::string>();
        }
    }
    if (!supersedes_run_id.empty()) {
        transaction_->execSqlSync(
            "update assessment_runs set is_current=false where id=$1::uuid",
            supersedes_run_id);
    }

    Json::Value result_summary;
    result_summary["result"] = assessment_result_to_json(result);
    result_summary["issues"] = Json::Value(Json::arrayValue);
    const auto inserted = transaction_->execSqlSync(
        "insert into assessment_runs(inspection_year_id,source_import_record_id,run_kind,"
        "formal_revision_number,supersedes_run_id,technical_condition_package_id,"
        "standard_profile_id,component_inventory_revision_id,result_status,is_current,"
        "rating_tree_version_id,rating_tree_content_checksum,"
        "input_summary_json,input_checksum,rule_package_summary_json,rule_package_checksum,"
        "result_summary_json,created_by_user_id) values($1::uuid,$2::uuid,'正式',$3,"
        "nullif($4,'')::uuid,$5::uuid,$6::uuid,$7::uuid,'运行中',false,$8::uuid,$9,"
        "$10::jsonb,$11,$12::jsonb,$13,'{}'::jsonb,$14::uuid) returning id::text as id",
        target_inspection_year_id,
        source_import_record_id,
        formal_revision,
        supersedes_run_id,
        context[0]["package_id"].as<std::string>(),
        context[0]["profile_id"].as<std::string>(),
        context[0]["inventory_revision_id"].as<std::string>(),
        context[0]["rating_tree_version_id"].as<std::string>(),
        context[0]["rating_tree_content_checksum"].as<std::string>(),
        compact_json(preview.input_summary),
        preview.input_checksum,
        compact_json(preview.standard_identity),
        preview.standard_identity["content_checksum"].asString(),
        confirmed_by_user_id);
    AssessmentConfirmationWritten written;
    written.assessment_run_id = inserted[0]["id"].as<std::string>();

    for (const auto& part : result.structure_parts) {
        for (const auto& category : part.categories) {
            for (const auto& component : category.components) {
                const double deduction = (std::max)(0.0, 100.0 - component.score);
                transaction_->execSqlSync(
                    "insert into assessment_component_results(assessment_run_id,"
                    "bridge_component_id,standard_component_category_id,structure_part,"
                    "score,deduction,result_json) values($1::uuid,$2::uuid,$3,$4,$5,$6,$7::jsonb)",
                    written.assessment_run_id,
                    component.component_instance_id,
                    component.component_type_id,
                    standards::to_string(component.structure_part),
                    component.score,
                    deduction,
                    compact_json(component_json(component)));
                ++written.component_results;
            }
            transaction_->execSqlSync(
                "insert into assessment_part_results(assessment_run_id,result_level,result_key,"
                "parent_result_key,structure_part,standard_component_category_id,score,grade,"
                "weight,result_json) values($1::uuid,'部件',$2,$3,$4,$5,$6,$7,$8,$9::jsonb)",
                written.assessment_run_id,
                category.component_type_id,
                standards::to_string(part.structure_part),
                standards::to_string(category.structure_part),
                category.component_type_id,
                category.score,
                std::to_string(category.grade) + "类",
                category.effective_weight,
                compact_json(category_json(category)));
            ++written.part_results;
        }
        Json::Value part_json;
        part_json["structure_part"] = standards::to_string(part.structure_part);
        part_json["score"] = part.score;
        part_json["grade"] = part.grade;
        part_json["overall_weight"] = part.overall_weight;
        transaction_->execSqlSync(
            "insert into assessment_part_results(assessment_run_id,result_level,result_key,"
            "structure_part,score,grade,weight,result_json) "
            "values($1::uuid,'结构',$2,$2,$3,$4,$5,$6::jsonb)",
            written.assessment_run_id,
            standards::to_string(part.structure_part),
            part.score,
            std::to_string(part.grade) + "类",
            part.overall_weight,
            compact_json(part_json));
        ++written.part_results;
    }
    transaction_->execSqlSync(
        "insert into assessment_part_results(assessment_run_id,result_level,result_key,"
        "structure_part,score,grade,result_json) "
        "values($1::uuid,'全桥','overall','overall',$2,$3,$4::jsonb)",
        written.assessment_run_id,
        result.overall_score,
        std::to_string(result.final_grade) + "类",
        compact_json(assessment_result_to_json(result)));
    ++written.part_results;

    for (const auto& control : result.triggered_controls) {
        Json::Value output;
        output["result_grade"] = control.result_grade.has_value()
            ? Json::Value(*control.result_grade)
            : Json::Value(Json::nullValue);
        output["source_reference"] = control.source_reference;
        transaction_->execSqlSync(
            "insert into assessment_control_results(assessment_run_id,rule_id,control_level,"
            "target_key,triggered,grade_after,message,input_json,output_json) "
            "values($1::uuid,$2,'全桥','overall',true,$3,$4,'{}'::jsonb,$5::jsonb)",
            written.assessment_run_id,
            control.control_id,
            control.result_grade.has_value()
                ? std::to_string(*control.result_grade) + "类"
                : std::string(),
            control.label,
            compact_json(output));
        ++written.control_results;
    }

    int sequence = 0;
    for (const auto& trace : result.trace) {
        ++sequence;
        auto rule_id = trace.rule_id;
        auto trace_stage = trace.step;
        auto target_type = trace_target_type(trace.step);
        auto target_key = trace.entity_id.empty()
            ? std::string("overall")
            : trace.entity_id;
        transaction_->execSqlSync(
            "insert into assessment_rule_traces(assessment_run_id,sequence_number,rule_id,"
            "trace_stage,target_type,target_key,input_json,output_json) "
            "values($1::uuid,$2,$3,$4,$5,$6,$7::jsonb,$8::jsonb)",
            written.assessment_run_id,
            sequence,
            rule_id,
            trace_stage,
            target_type,
            target_key,
            compact_json(trace.inputs),
            compact_json(trace.output));
        ++written.rule_traces;
    }
    if (preview.input_summary["rating_tree_skips"].isArray()) {
        for (const auto& skip :
             preview.input_summary["rating_tree_skips"]) {
            ++sequence;
            transaction_->execSqlSync(
                "insert into assessment_rule_traces(assessment_run_id,"
                "sequence_number,rule_id,trace_stage,target_type,target_key,"
                "input_json,output_json) "
                "values($1::uuid,$2,'rating_tree_non_scoring',"
                "'rating_tree_filter','defect',$3,$4::jsonb,$5::jsonb)",
                written.assessment_run_id,
                sequence,
                skip["candidate_id"].asString(),
                compact_json(skip),
                compact_json(skip));
            ++written.rule_traces;
        }
    }

    transaction_->execSqlSync(
        "update assessment_runs set result_status='成功',is_current=true,"
        "result_summary_json=$2::jsonb,confirmed_by_user_id=$3::uuid,confirmed_at=now(),"
        "updated_at=now() where id=$1::uuid",
        written.assessment_run_id,
        compact_json(result_summary),
        confirmed_by_user_id);

    for (const auto& part : result.structure_parts) {
        for (const auto& category : part.categories) {
            for (const auto& component : category.components) {
                transaction_->execSqlSync(
                    "insert into condition_ratings(inspection_year_id,source_import_record_id,"
                    "rating_level,structure_part,bridge_component_id,rating_item_name,score,"
                    "review_status,assessment_run_id) values($1::uuid,$2::uuid,'构件',$3,"
                    "$4::uuid,$5,$6,'已确认',$7::uuid)",
                    target_inspection_year_id,
                    source_import_record_id,
                    chinese_structure_part(component.structure_part),
                    component.component_instance_id,
                    component.component_instance_id,
                    component.score,
                    written.assessment_run_id);
                ++written.condition_rating_projections;
            }
            transaction_->execSqlSync(
                "insert into condition_ratings(inspection_year_id,source_import_record_id,"
                "rating_level,structure_part,rating_item_name,score,grade,weight,review_status,"
                "assessment_run_id) values($1::uuid,$2::uuid,'部件',$3,$4,$5,$6,$7,"
                "'已确认',$8::uuid)",
                target_inspection_year_id,
                source_import_record_id,
                chinese_structure_part(category.structure_part),
                category.component_type_id,
                category.score,
                std::to_string(category.grade) + "类",
                category.effective_weight,
                written.assessment_run_id);
            ++written.condition_rating_projections;
        }
        transaction_->execSqlSync(
            "insert into condition_ratings(inspection_year_id,source_import_record_id,"
            "rating_level,structure_part,rating_item_name,score,grade,weight,review_status,"
            "assessment_run_id) values($1::uuid,$2::uuid,'结构分部',$3,$3,$4,$5,$6,"
            "'已确认',$7::uuid)",
            target_inspection_year_id,
            source_import_record_id,
            chinese_structure_part(part.structure_part),
            part.score,
            std::to_string(part.grade) + "类",
            part.overall_weight,
            written.assessment_run_id);
        ++written.condition_rating_projections;
    }
    transaction_->execSqlSync(
        "insert into condition_ratings(inspection_year_id,source_import_record_id,"
        "rating_level,structure_part,rating_item_name,score,grade,review_status,"
        "assessment_run_id) values($1::uuid,$2::uuid,'全桥','全桥','全桥',$3,$4,"
        "'已确认',$5::uuid)",
        target_inspection_year_id,
        source_import_record_id,
        result.overall_score,
        std::to_string(result.final_grade) + "类",
        written.assessment_run_id);
    ++written.condition_rating_projections;
    return written;
}

}  // namespace bridge_report::assessment
