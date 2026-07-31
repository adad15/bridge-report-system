#include "bridge_report/review/DraftValidation.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "bridge_report/contracts/AnnualInspectionContract.hpp"
#include "bridge_report/rating_tree/RatingTreeResolver.hpp"
#include "bridge_report/review/DefectRatingTreeMatching.hpp"
#include "bridge_report/review/JsonAccessors.hpp"

namespace bridge_report::review {
namespace {

bool defect_has_warnings(const Json::Value& defect) {
    return defect["warnings"].isArray() && !defect["warnings"].empty();
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
        "source_structure_part", "component_name", "component_number", "defect_location",
        "defect_scale", "defect_type", "defect_description",
        "quantity_text", "measurement_text", "measurements", "review_status",
        "group_review_status", "review_note",
        "bridge_component_id", "standard_component_category_id", "resolved_structure_part",
        "component_inventory_revision_id", "component_match_candidate_ids",
        "component_match_method", "component_match_confirmed_by",
        "warnings",
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

    const auto contract_result =
        contracts::validate_bridge_annual_inspection_data(body);
    if (!contract_result.ok()) {
        result.ok = false;
        result.code = "contract_validation_failed";
        result.message = "请求体未通过契约校验。";
        for (const auto& issue : contract_result.issues()) {
            result.issues.push_back(DraftValidationIssue{issue.path, issue.message});
        }
        return result;
    }

    if (body["defects"].isArray()) {
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

namespace {

struct DefectRatingTreeScope {
    std::string bridge_type_id;
    std::string component_category_id;
};

std::optional<DefectRatingTreeScope> rating_tree_scope_for_defect(
    const Json::Value& defect,
    const std::string& technical_standard_package_id,
    const std::optional<inventory::InventoryRevision>& latest_revision) {
    if (!latest_revision.has_value()) return std::nullopt;
    const auto component_id = string_member_or_empty(defect, "bridge_component_id");
    for (const auto& entry : latest_revision->entries) {
        if (!entry.is_active || entry.bridge_component_id != component_id) continue;
        for (const auto& mapping : entry.mappings) {
            if (mapping.is_active &&
                mapping.confirmation_status == "已确认" &&
                mapping.standard_package_id == technical_standard_package_id) {
                return DefectRatingTreeScope{
                    mapping.standard_bridge_type_id,
                    mapping.standard_component_category_id};
            }
        }
    }
    return std::nullopt;
}

bool tree_node_applies(
    const rating_tree::EffectiveRatingTreeNode& node,
    const DefectRatingTreeScope& scope) {
    return node.node_type == rating_tree::RatingTreeNodeType::defect &&
        node.is_selectable &&
        std::find(
            node.bridge_type_ids.begin(),
            node.bridge_type_ids.end(),
            scope.bridge_type_id) != node.bridge_type_ids.end() &&
        std::find(
            node.component_category_ids.begin(),
            node.component_category_ids.end(),
            scope.component_category_id) != node.component_category_ids.end();
}

std::map<std::string, const Json::Value*> rating_tree_defect_index(
    const Json::Value& draft) {
    return index_defects_by_candidate_id(draft);
}

void clear_derived_rating_tree_fields(
    Json::Value& defect,
    const std::string& rating_tree_version_id) {
    defect["rating_tree_version_id"] = rating_tree_version_id;
    defect["rating_tree_node_id"] = Json::Value();
    defect["standard_defect_indicator_id"] = Json::Value();
    defect["rating_tree_match_method"] = Json::Value();
    defect["rating_tree_match_evidence"] = Json::Value();
}

bool same_component(
    const Json::Value* stored_defect,
    const Json::Value& new_defect) {
    return stored_defect != nullptr &&
        string_member_or_empty(*stored_defect, "bridge_component_id") ==
            string_member_or_empty(new_defect, "bridge_component_id");
}

bool is_auto_match_method(const std::string& method) {
    return method == "exact" || method == "controlled_alias" ||
        method == "controlled_keyword";
}

}  // namespace

DraftValidationResult normalize_defect_rating_tree_associations(
    Json::Value& draft,
    const Json::Value& stored_draft,
    const std::string& rating_tree_version_id,
    const std::string& technical_standard_package_id,
    const rating_tree::EffectiveRatingTree& tree,
    const std::optional<inventory::InventoryRevision>& latest_revision) {
    DraftValidationResult result;
    result.code = "defect_rating_tree_assignment_invalid";
    result.message = "病害选择的评定树节点不属于本年度，或不适用于当前实际构件。";
    if (!draft["defects"].isArray()) {
        result.ok = true;
        result.code.clear();
        result.message.clear();
        return result;
    }
    const auto stored = rating_tree_defect_index(stored_draft);
    const rating_tree::RatingTreeResolver resolver;
    // 用户提交了节点的病害在这里就地定稿；其余交给统一的批量匹配服务，
    // 保证导入、绑定、草稿保存和页面重新匹配跑的是同一份规则。
    DefectMatchScope auto_scope;
    auto_scope.has_scope = true;
    for (Json::ArrayIndex index = 0; index < draft["defects"].size(); ++index) {
        auto& defect = draft["defects"][index];
        const auto candidate_id = string_member_or_empty(defect, "candidate_id");
        const auto stored_it = stored.find(candidate_id);
        const Json::Value* stored_defect =
            stored_it == stored.end() ? nullptr : stored_it->second;
        const auto submitted_node =
            string_member_or_empty(defect, "rating_tree_node_id");
        const auto submitted_method =
            string_member_or_empty(defect, "rating_tree_match_method");
        const bool component_unchanged = same_component(stored_defect, defect);
        const auto scope = rating_tree_scope_for_defect(
            defect, technical_standard_package_id, latest_revision);

        if (submitted_node.empty() || !component_unchanged) {
            clear_derived_rating_tree_fields(defect, rating_tree_version_id);
            if (scope.has_value()) auto_scope.candidate_ids.insert(candidate_id);
            continue;
        }

        clear_derived_rating_tree_fields(defect, rating_tree_version_id);
        if (!scope.has_value()) continue;
        const auto node = tree.nodes.find(submitted_node);
        if (node == tree.nodes.end() || !tree_node_applies(node->second, *scope)) {
            result.issues.push_back({
                "defects[" + std::to_string(index) + "].rating_tree_node_id",
                "所选评定树节点不属于本年度或不适用于当前实际构件。"});
            continue;
        }
        defect["rating_tree_node_id"] = submitted_node;
        defect["standard_defect_indicator_id"] =
            node->second.h21_indicator_id.has_value()
                ? Json::Value(*node->second.h21_indicator_id)
                : Json::Value();

        const auto stored_node = stored_defect == nullptr
            ? std::string{}
            : string_member_or_empty(*stored_defect, "rating_tree_node_id");
        const auto stored_method = stored_defect == nullptr
            ? std::string{}
            : string_member_or_empty(*stored_defect, "rating_tree_match_method");
        // 已经在库里的自动结果原样保留。页面把本次批量匹配的自动结果先落进本地
        // 草稿时库里还没有节点，这时重跑一次匹配器：结论一致才认自动方式，
        // 否则一律记成人工选择，避免客户端伪造自动匹配证据。
        if (stored_node == submitted_node && is_auto_match_method(stored_method)) {
            defect["rating_tree_match_method"] = stored_method;
            defect["rating_tree_match_evidence"] =
                (*stored_defect)["rating_tree_match_evidence"];
            continue;
        }
        if (is_auto_match_method(submitted_method)) {
            rating_tree::RatingTreeMatchInput input;
            input.bridge_type_id = scope->bridge_type_id;
            input.component_category_id = scope->component_category_id;
            input.defect_type = string_member_or_empty(defect, "defect_type");
            input.defect_description =
                string_member_or_empty(defect, "defect_description");
            input.defect_location =
                string_member_or_empty(defect, "defect_location");
            const auto verified = resolver.resolve(tree, input);
            if (verified.outcome ==
                    rating_tree::RatingTreeMatchOutcome::auto_bound &&
                verified.node_id.value_or("") == submitted_node) {
                defect["rating_tree_match_method"] = verified.match_method;
                defect["rating_tree_match_evidence"] = verified.match_evidence;
                continue;
            }
        }
        defect["rating_tree_match_method"] = "manual";
        defect["rating_tree_match_evidence"] =
            Json::Value("用户在当前实际构件范围内选择了该评定树节点。");
    }

    if (!auto_scope.candidate_ids.empty()) {
        (void)match_defect_rating_tree_nodes(
            draft,
            rating_tree_version_id,
            technical_standard_package_id,
            tree,
            latest_revision,
            auto_scope,
            true);
    }
    result.ok = result.issues.empty();
    if (result.ok) {
        result.code.clear();
        result.message.clear();
    }
    return result;
}

DraftValidationResult validate_defect_rating_tree_for_confirmation(
    const Json::Value& draft,
    const std::string& rating_tree_version_id,
    const std::string& technical_standard_package_id,
    const rating_tree::EffectiveRatingTree& tree,
    const std::optional<inventory::InventoryRevision>& latest_revision) {
    DraftValidationResult result;
    result.code = "defect_rating_tree_invalid";
    result.message = "病害的评定树节点、构件适用范围或标度不满足正式入库要求。";
    if (!draft["defects"].isArray()) {
        result.ok = true;
        result.code.clear();
        result.message.clear();
        return result;
    }
    for (Json::ArrayIndex index = 0; index < draft["defects"].size(); ++index) {
        const auto& defect = draft["defects"][index];
        const auto review_status = string_member_or_empty(defect, "review_status");
        if (review_status == "已忽略") continue;
        if ((review_status != "已确认" && review_status != "已修改") ||
            string_member_or_empty(defect, "group_review_status") != "已确认") {
            continue;
        }
        const auto path =
            "defects[" + std::to_string(index) + "].rating_tree_node_id";
        const auto scope = rating_tree_scope_for_defect(
            defect, technical_standard_package_id, latest_revision);
        const auto node_id =
            string_member_or_empty(defect, "rating_tree_node_id");
        const auto version_id =
            string_member_or_empty(defect, "rating_tree_version_id");
        const auto node = tree.nodes.find(node_id);
        if (!scope.has_value() || version_id != rating_tree_version_id ||
            node == tree.nodes.end() ||
            !tree_node_applies(node->second, *scope)) {
            result.issues.push_back(
                {path, "请选择当前年度且适用于实际构件的评定树病害节点。"});
            continue;
        }
        const auto submitted_h21 =
            string_member_or_empty(defect, "standard_defect_indicator_id");
        if (submitted_h21 != node->second.h21_indicator_id.value_or("")) {
            result.issues.push_back(
                {path, "评定树节点解析出的 H21 指标与草稿不一致。"});
        }
        if (node->second.is_scoring) {
            if (!defect["defect_scale"].isIntegral() ||
                std::find(
                    node->second.allowed_scales.begin(),
                    node->second.allowed_scales.end(),
                    defect["defect_scale"].asInt()) ==
                    node->second.allowed_scales.end()) {
                result.issues.push_back({
                    "defects[" + std::to_string(index) + "].defect_scale",
                    "病害标度不在该评定树节点允许的标度范围内。"});
            }
        }
    }
    result.ok = result.issues.empty();
    if (result.ok) {
        result.code.clear();
        result.message.clear();
    }
    return result;
}

DraftValidationResult validate_imported_defect_evidence(
    const Json::Value& stored_draft,
    const Json::Value& new_draft) {
    DraftValidationResult result;
    const auto stored = index_defects_by_candidate_id(stored_draft);
    const auto current = index_defects_by_candidate_id(new_draft);
    for (const auto& [candidate_id, stored_defect] : stored) {
        const auto current_it = current.find(candidate_id);
        if (current_it == current.end()) {
            continue;
        }
        const auto* current_defect = current_it->second;
        if (!json_semantically_equal(
                (*stored_defect)["source_ref"],
                (*current_defect)["source_ref"]) ||
            !json_semantically_equal(
                (*stored_defect)["range_split_origin"],
                (*current_defect)["range_split_origin"])) {
            result.ok = false;
            result.code = "imported_evidence_modified";
            result.message = "Word 来源证据和范围拆分来源不可修改。";
            result.issues.push_back({
                "defects." + candidate_id + ".source_ref",
                "source_ref 或 range_split_origin 与服务端保存的来源不一致。"});
        }
    }
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
        if (member == "defects") {
            continue;
        }
        if (!new_draft.isMember(member)
            || !json_semantically_equal(stored_draft[member], new_draft[member])) {
            result.issues.push_back({member, "warnings_only 重开不允许修改 " + member + "。"});
        }
    }
    for (const auto& member : new_draft.getMemberNames()) {
        if (member != "defects" && !stored_draft.isMember(member)) {
            result.issues.push_back({member, "warnings_only 重开不允许新增顶层字段 " + member + "。"});
        }
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
