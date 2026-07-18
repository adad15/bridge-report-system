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
        "structure_part", "component_name", "component_alias", "defect_location",
        "defect_scale", "defect_deduction", "defect_type", "defect_description",
        "quantity_text", "measurement_text", "measurements", "review_status",
        "group_review_status", "review_note",
    };
    return fields;
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
    rebuild_component_ratings(expected);

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
            {"ratings", "评分数据必须保持不变，或严格等于警告病害扣分变化产生的后端复算结果。"});
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

}  // 命名空间 bridge_report::review
