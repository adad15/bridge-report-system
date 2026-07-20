#include "bridge_report/review/DraftValidation.hpp"

#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "bridge_report/contracts/AnnualInspectionContract.hpp"
#include "bridge_report/review/ComponentScore.hpp"
#include "bridge_report/review/JsonAccessors.hpp"

namespace bridge_report::review {
namespace {

bool defect_has_warnings(const Json::Value& defect) {
    return defect["warnings"].isArray() && !defect["warnings"].empty();
}

std::optional<double> optional_numeric_member(const Json::Value& object, const char* member) {
    if (!object.isObject() || !object.isMember(member) || !object[member].isNumeric()) {
        return std::nullopt;
    }
    return object[member].asDouble();
}

// 数值语义等价的深比较：jsonb -> 文本 -> 前端 JSON.parse/stringify 的往返会把
// 1.0（real）变成 1（int），Json::Value::operator== 视为不等；这里对数值统一按
// double 比较，避免"未改动的病害被判为已修改"的误报。
bool json_semantically_equal(const Json::Value& a, const Json::Value& b) {
    if (a.isNumeric() && b.isNumeric()) {
        return a.asDouble() == b.asDouble();
    }
    if (a.type() != b.type()) {
        return false;
    }
    if (a.isArray()) {
        if (a.size() != b.size()) {
            return false;
        }
        for (Json::ArrayIndex i = 0; i < a.size(); ++i) {
            if (!json_semantically_equal(a[i], b[i])) {
                return false;
            }
        }
        return true;
    }
    if (a.isObject()) {
        const auto a_members = a.getMemberNames();
        if (a_members.size() != b.getMemberNames().size()) {
            return false;
        }
        for (const auto& member : a_members) {
            if (!b.isMember(member) || !json_semantically_equal(a[member], b[member])) {
                return false;
            }
        }
        return true;
    }
    return a == b;
}

// 按 candidate_id 建立病害候选索引；缺 candidate_id 的条目跳过
// （契约校验会另行拦截，这里不重复报错）。
std::map<std::string, const Json::Value*> index_defects_by_candidate_id(const Json::Value& data) {
    std::map<std::string, const Json::Value*> index;
    if (!data["defects"].isArray()) {
        return index;
    }
    for (const auto& defect : data["defects"]) {
        if (defect["candidate_id"].isString()) {
            index.emplace(defect["candidate_id"].asString(), &defect);
        }
    }
    return index;
}

const std::set<std::string>& warning_defect_editable_fields() {
    static const std::set<std::string> fields = {
        "structure_part", "component_name", "component_alias", "component_number", "defect_location",
        "defect_scale", "defect_deduction", "defect_type", "defect_description",
        "quantity_text", "measurement_text", "measurements", "review_status",
        "group_review_status", "review_note",
        "bridge_component_id", "standard_component_category_id", "resolved_structure_part",
        "component_inventory_revision_id", "component_match_candidate_ids",
        "component_match_method", "component_match_confirmed_by",
    };
    return fields;
}

std::string contract_structure_part(const std::string& value) {
    if (value == "superstructure") return "上部结构";
    if (value == "substructure") return "下部结构";
    if (value == "deck_system") return "桥面系";
    if (value == "overall") return "全桥";
    return "其他";
}

Json::Value frozen_warning_defect_fields(Json::Value defect) {
    for (const auto& field : warning_defect_editable_fields()) {
        defect.removeMember(field);
    }
    return defect;
}

bool component_ref_matches_defect(const Json::Value& component_ref, const Json::Value& defect) {
    return string_member_or_empty(component_ref, "structure_part") == string_member_or_empty(defect, "structure_part")
        && string_member_or_empty(component_ref, "component_name") == string_member_or_empty(defect, "component_name")
        && string_member_or_empty(component_ref, "component_alias") == string_member_or_empty(defect, "component_alias");
}

bool same_string_array(const Json::Value& current, const Json::Value& expected) {
    return current.isArray() && expected.isArray() && json_semantically_equal(current, expected);
}

void rebuild_component_ratings(Json::Value& draft) {
    if (!draft.isObject() || !draft.isMember("ratings") || !draft["ratings"].isObject()
        || !draft["ratings"].isMember("component_ratings")) {
        return;
    }
    auto& component_ratings = draft["ratings"]["component_ratings"];
    if (!component_ratings.isArray() || !draft["defects"].isArray()) {
        return;
    }

    for (auto& rating : component_ratings) {
        Json::Value deduction_ids(Json::arrayValue);
        std::vector<double> deductions;
        bool deductions_complete = true;
        for (const auto& defect : draft["defects"]) {
            if (review_status_of(defect) == "已忽略"
                || !component_ref_matches_defect(rating["component_ref"], defect)) {
                continue;
            }
            deduction_ids.append(candidate_id_of(defect));
            const auto deduction = optional_numeric_member(defect, "defect_deduction");
            if (deduction.has_value()) {
                deductions.push_back(*deduction);
            } else {
                deductions_complete = false;
            }
        }

        const auto recalculated = deductions_complete && !deductions.empty()
            ? compute_component_score(deductions)
            : std::nullopt;
        const auto current_calculated = optional_numeric_member(rating, "calculated_score");
        const bool same_calculated = (!recalculated.has_value() && !current_calculated.has_value())
            || (recalculated.has_value() && current_calculated.has_value()
                && std::abs(recalculated->score - *current_calculated) <= 1e-9);
        if (same_string_array(rating["deduction_defect_candidate_ids"], deduction_ids) && same_calculated) {
            continue;
        }

        rating["deduction_defect_candidate_ids"] = deduction_ids;
        if (recalculated.has_value()) {
            rating["calculated_score"] = recalculated->score;
            Json::Value details(Json::objectValue);
            details["standard"] = "JTG/T H21-2011 4.1.1";
            details["rounding_scale"] = 2;
            details["ordered_deductions"] = Json::Value(Json::arrayValue);
            for (const auto deduction : recalculated->ordered_deductions) {
                details["ordered_deductions"].append(deduction);
            }
            rating["calculation_details"] = std::move(details);
        } else {
            rating["calculated_score"] = Json::Value(Json::nullValue);
            rating["calculation_details"] = Json::Value(Json::nullValue);
        }

        const auto source = optional_numeric_member(rating, "source_score");
        const auto status = classify_score_validation(
            source,
            recalculated.has_value() ? std::optional<double>(recalculated->score) : std::nullopt
        );
        rating["score_validation_status"] = status;
        if (status == "一致" && source.has_value()) {
            rating["confirmed_score"] = round_score_to_two_decimals(*source);
        } else {
            rating["confirmed_score"] = Json::Value(Json::nullValue);
        }
        rating["score_resolution_reason"] = Json::Value(Json::nullValue);
        rating["review_status"] = "待确认";
    }
}

}  // namespace

DraftValidationResult validate_review_draft(
    const Json::Value& body,
    const std::string& record_system_number,
    const std::string& record_import_status
) {
    DraftValidationResult result;

    if (record_import_status != "待校对") {
        result.ok = false;
        result.code = "import_record_not_editable";
        result.message = "导入记录当前状态不是待校对，无法保存草稿。";
        return result;
    }

    const auto mode = body["contract"]["version"].isString() &&
                              body["contract"]["version"].asString() == "1.2"
                          ? contracts::AnnualInspectionValidationMode::Legacy12Transition
                          : contracts::AnnualInspectionValidationMode::FinalVersion2;
    const auto contract_result =
        contracts::validate_bridge_annual_inspection_data(body, mode);
    if (!contract_result.ok()) {
        result.ok = false;
        result.code = "contract_validation_failed";
        result.message = "请求体未通过契约校验。";
        for (const auto& issue : contract_result.issues()) {
            result.issues.push_back(DraftValidationIssue{issue.path, issue.message});
        }
        return result;
    }

    if (mode == contracts::AnnualInspectionValidationMode::FinalVersion2 && body["defects"].isArray()) {
        for (Json::ArrayIndex index = 0; index < body["defects"].size(); ++index) {
            const auto& defect = body["defects"][index];
            const auto has_non_blank_string = [&](const char* field) {
                return defect.isMember(field) && defect[field].isString() && !defect[field].asString().empty();
            };
            const bool has_component_id = has_non_blank_string("bridge_component_id");
            const bool has_category_id = has_non_blank_string("standard_component_category_id");
            const bool has_resolved_part = has_non_blank_string("resolved_structure_part");
            if ((has_component_id || has_category_id || has_resolved_part)
                && !(has_component_id && has_category_id && has_resolved_part)) {
                result.ok = false;
                result.code = "defect_component_mapping_invalid";
                result.message = "病害的实际构件、规范构件类别和内部结构分部必须成组提供。";
                result.issues.push_back({
                    "defects[" + std::to_string(index) + "]",
                    "bridge_component_id、standard_component_category_id、resolved_structure_part 不完整。"
                });
                return result;
            }
        }
    }

    const auto& import_context = body["import_context"];
    const auto body_system_number = import_context.isObject() && import_context.isMember("import_record_system_number")
        ? import_context["import_record_system_number"].asString()
        : std::string();
    if (body_system_number != record_system_number) {
        result.ok = false;
        result.code = "import_context_mismatch";
        result.message = "请求体的导入记录编号与目标记录不一致。";
        return result;
    }

    result.ok = true;
    return result;
}

DraftValidationResult validate_defect_component_associations(
    const Json::Value& body,
    const std::optional<inventory::InventoryRevision>& latest_revision) {
    DraftValidationResult result;
    result.code = "defect_component_assignment_invalid";
    result.message = "病害关联的实际构件不属于当前桥梁最新台账，或规范映射已变化。";
    if (!body["defects"].isArray()) {
        result.ok = true;
        result.code.clear();
        result.message.clear();
        return result;
    }

    for (Json::ArrayIndex index = 0; index < body["defects"].size(); ++index) {
        const auto& defect = body["defects"][index];
        const auto component_id = string_member_or_empty(defect, "bridge_component_id");
        const auto category_id = string_member_or_empty(defect, "standard_component_category_id");
        const auto structure_part = string_member_or_empty(defect, "resolved_structure_part");
        const auto revision_id = string_member_or_empty(defect, "component_inventory_revision_id");
        const auto path = "defects[" + std::to_string(index) + "].bridge_component_id";

        if (component_id.empty()) {
            if (!category_id.empty() || !structure_part.empty()) {
                result.issues.push_back({path, "未选择实际构件时不能提交规范类别或内部结构部位。"});
            }
            continue;
        }
        if (!latest_revision.has_value() || revision_id != latest_revision->id) {
            result.issues.push_back({path, "关联所依据的构件台账已变化，请重新选择。"});
            continue;
        }
        const inventory::InventoryEntry* matched_entry = nullptr;
        for (const auto& entry : latest_revision->entries) {
            if (entry.is_active && entry.bridge_component_id == component_id) {
                matched_entry = &entry;
                break;
            }
        }
        if (matched_entry == nullptr) {
            result.issues.push_back({path, "实际构件不属于当前桥梁最新台账。"});
            continue;
        }
        bool mapping_matches = false;
        for (const auto& mapping : matched_entry->mappings) {
            if (mapping.is_active &&
                mapping.standard_component_category_id == category_id &&
                contract_structure_part(mapping.structure_part) == structure_part) {
                mapping_matches = true;
                break;
            }
        }
        if (!mapping_matches) {
            result.issues.push_back({path, "实际构件的规范类别或内部结构部位与最新映射不一致。"});
        }
    }

    result.ok = result.issues.empty();
    if (result.ok) {
        result.code.clear();
        result.message.clear();
    }
    return result;
}

bool draft_has_warning_defects(const Json::Value& data) {
    if (!data["defects"].isArray()) {
        return false;
    }
    for (const auto& defect : data["defects"]) {
        if (defect_has_warnings(defect)) {
            return true;
        }
    }
    return false;
}

DraftValidationResult validate_warnings_only_scope(
    const Json::Value& stored_draft,
    const Json::Value& new_draft,
    Json::Value* normalized_draft
) {
    DraftValidationResult result;
    result.code = "reopen_scope_violation";
    result.message = "重开范围为仅警告病害，其余病害候选不可增删或修改。";

    const auto stored_index = index_defects_by_candidate_id(stored_draft);
    const auto new_index = index_defects_by_candidate_id(new_draft);

    for (const auto& [candidate_id, stored_defect] : stored_index) {
        const auto new_it = new_index.find(candidate_id);
        if (new_it == new_index.end()) {
            result.issues.push_back({"defects", "病害候选 " + candidate_id + " 被删除。"});
            continue;
        }
        if (!defect_has_warnings(*stored_defect)) {
            if (!json_semantically_equal(*stored_defect, *new_it->second)) {
                result.issues.push_back(
                    {"defects", "病害候选 " + candidate_id + " 无警告，重开范围内不可修改。"});
            }
        } else if (!json_semantically_equal(
                       frozen_warning_defect_fields(*stored_defect),
                       frozen_warning_defect_fields(*new_it->second))) {
            result.issues.push_back(
                {"defects", "病害候选 " + candidate_id + " 修改了 warnings_only 不允许的证据或照片字段。"});
        }
    }
    for (const auto& [candidate_id, new_defect] : new_index) {
        (void)new_defect;
        if (stored_index.find(candidate_id) == stored_index.end()) {
            result.issues.push_back({"defects", "病害候选 " + candidate_id + " 为新增，重开范围内不允许。"});
        }
    }

    Json::Value expected = stored_draft;
    expected["defects"] = new_draft["defects"];

    for (const auto& member : stored_draft.getMemberNames()) {
        if (member == "defects" || member == "ratings") {
            continue;
        }
        if (!new_draft.isMember(member)
            || !json_semantically_equal(stored_draft[member], new_draft[member])) {
            result.issues.push_back({member, "warnings_only 重开不允许修改 " + member + "。"});
        }
    }
    for (const auto& member : new_draft.getMemberNames()) {
        if (member != "defects" && member != "ratings" && !stored_draft.isMember(member)) {
            result.issues.push_back({member, "warnings_only 重开不允许新增顶层字段 " + member + "。"});
        }
    }
    if (!json_semantically_equal(expected["ratings"], new_draft["ratings"])) {
        result.issues.push_back(
            {"ratings", "Word 评分仅供报告对照，warnings_only 重开不允许修改。"});
    }

    result.ok = result.issues.empty();
    if (result.ok) {
        result.code.clear();
        result.message.clear();
        if (normalized_draft != nullptr) {
            *normalized_draft = std::move(expected);
        }
    }
    return result;
}

Json::Value build_defect_change_audit_event(
    const Json::Value& stored_draft,
    const Json::Value& new_draft,
    const std::string& actor_username
) {
    const auto stored = index_defects_by_candidate_id(stored_draft);
    const auto current = index_defects_by_candidate_id(new_draft);
    Json::Value added(Json::arrayValue);
    Json::Value deleted(Json::arrayValue);
    for (const auto& [candidate_id, defect] : current) {
        (void)defect;
        if (stored.find(candidate_id) == stored.end()) added.append(candidate_id);
    }
    for (const auto& [candidate_id, defect] : stored) {
        (void)defect;
        if (current.find(candidate_id) == current.end()) deleted.append(candidate_id);
    }
    if (added.empty() && deleted.empty()) return Json::Value(Json::nullValue);

    Json::Value event(Json::objectValue);
    event["event_type"] = "draft_defect_structure_change";
    event["actor_username"] = actor_username;
    event["added_candidate_ids"] = std::move(added);
    event["deleted_candidate_ids"] = std::move(deleted);
    return event;
}

}  // 命名空间 bridge_report::review
