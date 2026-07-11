#include "bridge_report/review/PreflightReport.hpp"

#include "bridge_report/contracts/AnnualInspectionContract.hpp"
#include "bridge_report/review/JsonAccessors.hpp"

namespace bridge_report::review {

namespace {

constexpr const char* kPending = "待确认";
constexpr const char* kIgnored = "已忽略";

constexpr const char* kMatchHighConfidence = "高置信候选";
constexpr const char* kMatchConfirmed = "已确认";
constexpr const char* kMatchUnlinked = "未关联";

bool photo_number_has_confirmed_link(const Json::Value& data, const std::string& defect_id, const std::string& photo_number);

void add_issue(std::vector<PreflightIssue>& target, std::string code, std::string message, std::string candidate_id = std::string()) {
    target.push_back(PreflightIssue{std::move(code), std::move(message), std::move(candidate_id)});
}

// -----------------------------------------------------------------------
// 检查 1：导入记录状态
// -----------------------------------------------------------------------

void check_import_status(const PreflightContext& context, std::vector<PreflightIssue>& blocking) {
    if (context.import_status != "待校对") {
        add_issue(blocking, "import_record_wrong_status",
                  "导入记录当前状态为「" + context.import_status + "」，不是待校对，无法执行入库前检查。");
    }
}

// -----------------------------------------------------------------------
// 检查 3：导入上下文一致性
// -----------------------------------------------------------------------

void check_import_context_mismatch(const Json::Value& data, const PreflightContext& context, std::vector<PreflightIssue>& blocking) {
    const auto record_number = string_member_or_empty(data["import_context"], "import_record_system_number");
    if (record_number != context.record_system_number) {
        add_issue(blocking, "import_context_mismatch",
                  "请求体的导入记录编号「" + record_number + "」与目标记录「" + context.record_system_number + "」不一致。");
        return;
    }

    const auto bridge_number = string_member_or_empty(data["bridge_check"], "selected_bridge_system_number");
    if (bridge_number != context.bridge_system_number) {
        add_issue(blocking, "import_context_mismatch",
                  "请求体选定的桥梁编号「" + bridge_number + "」与目标桥梁「" + context.bridge_system_number + "」不一致。");
        return;
    }

    if (context.inspection_year.has_value()) {
        const auto& inspection = data["inspection"];
        const bool year_matches = inspection.isObject() && inspection.isMember("inspection_year")
            && inspection["inspection_year"].isNumeric()
            && inspection["inspection_year"].asInt() == *context.inspection_year;
        if (!year_matches) {
            add_issue(blocking, "import_context_mismatch",
                      "请求体的检测年度与记录已挂年度「" + std::to_string(*context.inspection_year) + "」不一致。");
        }
    }
}

// -----------------------------------------------------------------------
// 检查 4：仍有候选处于待确认状态
// -----------------------------------------------------------------------

void check_candidate_pending_review(const Json::Value& data, std::vector<PreflightIssue>& blocking) {
    if (data["defects"].isArray()) {
        for (const auto& defect : data["defects"]) {
            if (review_status_of(defect) == kPending) {
                add_issue(blocking, "candidate_pending_review",
                          "病害候选 " + candidate_id_of(defect) + " 仍处于待确认状态。", candidate_id_of(defect));
            }
        }
    }

    if (data["photos"].isArray()) {
        for (const auto& photo : data["photos"]) {
            if (review_status_of(photo) == kPending) {
                add_issue(blocking, "candidate_pending_review",
                          "照片候选 " + candidate_id_of(photo) + " 仍处于待确认状态。", candidate_id_of(photo));
            }
        }
    }

    const auto& ratings = data["ratings"];
    if (ratings.isObject()) {
        if (ratings.isMember("overall") && review_status_of(ratings["overall"]) == kPending) {
            add_issue(blocking, "candidate_pending_review", "全桥评分仍处于待确认状态。", "ratings.overall");
        }
        if (ratings["structure_parts"].isArray()) {
            const auto& structure_parts = ratings["structure_parts"];
            for (Json::ArrayIndex index = 0; index < structure_parts.size(); ++index) {
                if (review_status_of(structure_parts[index]) == kPending) {
                    const auto target = "ratings.structure_parts[" + std::to_string(index) + "]";
                    add_issue(blocking, "candidate_pending_review", "结构分部评分 " + target + " 仍处于待确认状态。", target);
                }
            }
        }
        if (ratings["evaluation_parts"].isArray()) {
            const auto& evaluation_parts = ratings["evaluation_parts"];
            for (Json::ArrayIndex index = 0; index < evaluation_parts.size(); ++index) {
                if (review_status_of(evaluation_parts[index]) == kPending) {
                    const auto target = "ratings.evaluation_parts[" + std::to_string(index) + "]";
                    add_issue(blocking, "candidate_pending_review", "评价部件评分 " + target + " 仍处于待确认状态。", target);
                }
            }
        }
    }
}

// -----------------------------------------------------------------------
// 检查 5：已确认/已修改病害缺核心字段
// -----------------------------------------------------------------------

bool is_blank_string_field(const Json::Value& object, const char* key) {
    if (!object.isObject() || !object.isMember(key) || !object[key].isString()) {
        return true;
    }
    return object[key].asString().empty();
}

void check_defect_missing_required_field(const Json::Value& data, std::vector<PreflightIssue>& blocking) {
    if (!data["defects"].isArray()) {
        return;
    }
    for (const auto& defect : data["defects"]) {
        if (!is_review_settled(review_status_of(defect))) {
            continue;
        }
        static const char* required_fields[] = {"structure_part", "component_name", "defect_type", "defect_description"};
        for (const char* field : required_fields) {
            if (is_blank_string_field(defect, field)) {
                add_issue(blocking, "defect_missing_required_field",
                          "已确认病害 " + candidate_id_of(defect) + " 缺少必填字段 " + field + "。", candidate_id_of(defect));
                break;  // 每个病害最多一条阻断，字段名已在 message 中说明。
            }
        }
    }
}

// -----------------------------------------------------------------------
// 检查 6：已确认/已修改照片的病害关联是否能解析
// -----------------------------------------------------------------------

// 契约不保证 candidate_id 唯一性；若存在重复 id，此处按首个匹配处理。
const Json::Value* find_defect_by_candidate_id(const Json::Value& data, const std::string& candidate_id) {
    if (!data["defects"].isArray()) {
        return nullptr;
    }
    for (const auto& defect : data["defects"]) {
        if (candidate_id_of(defect) == candidate_id) {
            return &defect;
        }
    }
    return nullptr;
}

bool photo_requires_resolved_link(const Json::Value& photo) {
    const auto match_status = string_member_or_empty(photo, "match_status");
    return match_status == kMatchHighConfidence || match_status == kMatchConfirmed;
}

void check_photo_link_unresolved(const Json::Value& data, std::vector<PreflightIssue>& blocking) {
    if (!data["photos"].isArray()) {
        return;
    }
    for (const auto& photo : data["photos"]) {
        if (!is_review_settled(review_status_of(photo)) || !photo_requires_resolved_link(photo)) {
            continue;
        }

        const bool has_link = photo.isObject() && photo.isMember("linked_defect_candidate_id")
            && photo["linked_defect_candidate_id"].isString() && !photo["linked_defect_candidate_id"].asString().empty();

        if (!has_link) {
            add_issue(blocking, "photo_link_unresolved",
                      "照片候选 " + candidate_id_of(photo) + " 未关联到任何病害。", candidate_id_of(photo));
            continue;
        }

        const auto linked_id = photo["linked_defect_candidate_id"].asString();
        const auto* defect = find_defect_by_candidate_id(data, linked_id);
        if (defect == nullptr) {
            add_issue(blocking, "photo_link_unresolved",
                      "照片候选 " + candidate_id_of(photo) + " 关联的病害 " + linked_id + " 不存在。", candidate_id_of(photo));
            continue;
        }

        const auto defect_status = review_status_of(*defect);
        if (defect_status == kIgnored || defect_status == kPending) {
            add_issue(blocking, "photo_link_unresolved",
                      "照片候选 " + candidate_id_of(photo) + " 关联的病害 " + linked_id + " 处于「" + defect_status + "」状态。",
                      candidate_id_of(photo));
        }
    }
}

bool string_array_contains(const Json::Value& values, const std::string& expected) {
    if (!values.isArray()) {
        return false;
    }
    for (const auto& value : values) {
        if (value.isString() && value.asString() == expected) {
            return true;
        }
    }
    return false;
}

void check_defect_photo_groups(const Json::Value& data, std::vector<PreflightIssue>& blocking) {
    if (!data["defects"].isArray()) {
        return;
    }

    for (const auto& defect : data["defects"]) {
        if (!is_review_settled(review_status_of(defect))) {
            continue;
        }
        const auto defect_id = candidate_id_of(defect);
        if (string_member_or_empty(defect, "group_review_status") != "已确认") {
            add_issue(blocking, "group_confirmation_required",
                      "病害候选 " + defect_id + " 尚未完成病害与照片联合确认。", defect_id);
        }

        if (!defect["photo_numbers"].isArray()) {
            continue;
        }
        for (const auto& number_value : defect["photo_numbers"]) {
            if (!number_value.isString()) {
                continue;
            }
            const auto number = number_value.asString();
            if (!photo_number_has_confirmed_link(data, defect_id, number)
                && !string_array_contains(defect["confirmed_missing_photo_numbers"], number)) {
                add_issue(blocking, "missing_photo_confirmation_required",
                          "病害候选 " + defect_id + " 引用的照片 " + number + " 缺失，尚未人工确认。", defect_id);
            }
        }
    }
}

void check_photo_archives(const Json::Value& data, std::vector<PreflightIssue>& blocking) {
    if (!data["photos"].isArray()) {
        return;
    }
    for (const auto& photo : data["photos"]) {
        if (!is_review_settled(review_status_of(photo))
            || string_member_or_empty(photo, "match_status") != kMatchConfirmed
            || string_member_or_empty(photo, "linked_defect_candidate_id").empty()) {
            continue;
        }
        if (string_member_or_empty(photo["extracted_file"], "archive_relative_path").empty()) {
            add_issue(blocking, "photo_archive_missing",
                      "照片候选 " + candidate_id_of(photo) + " 缺少归档文件，无法入库。", candidate_id_of(photo));
        }
    }
}

// -----------------------------------------------------------------------
// 检查 7：全桥评分缺总分或等级
// -----------------------------------------------------------------------

void check_rating_overall_missing(const Json::Value& data, std::vector<PreflightIssue>& blocking) {
    const auto& ratings = data["ratings"];
    if (!ratings.isObject() || !ratings.isMember("overall") || !ratings["overall"].isObject()) {
        add_issue(blocking, "rating_overall_missing", "全桥评分对象缺失。");
        return;
    }

    const auto& overall = ratings["overall"];
    const bool has_total_score = overall.isMember("total_score") && overall["total_score"].isNumeric();
    const bool has_grade = overall.isMember("overall_grade") && overall["overall_grade"].isString()
        && !overall["overall_grade"].asString().empty();

    if (!has_total_score || !has_grade) {
        add_issue(blocking, "rating_overall_missing", "全桥评分缺少总分或等级。");
    }
}

// -----------------------------------------------------------------------
// 警告：已确认/已修改病害未关联任何已确认照片
// -----------------------------------------------------------------------

bool photo_number_has_confirmed_link(const Json::Value& data, const std::string& defect_id, const std::string& photo_number) {
    if (!data["photos"].isArray()) {
        return false;
    }
    for (const auto& photo : data["photos"]) {
        if (!is_review_settled(review_status_of(photo))) {
            continue;
        }
        if (string_member_or_empty(photo, "linked_defect_candidate_id") != defect_id) {
            continue;
        }
        if (string_member_or_empty(photo, "photo_number") == photo_number) {
            return true;
        }
    }
    return false;
}

void check_defect_without_photo(const Json::Value& data, std::vector<PreflightIssue>& warnings) {
    if (!data["defects"].isArray()) {
        return;
    }
    for (const auto& defect : data["defects"]) {
        if (!is_review_settled(review_status_of(defect))) {
            continue;
        }

        const bool has_photo_numbers = defect.isObject() && defect.isMember("photo_numbers")
            && defect["photo_numbers"].isArray() && !defect["photo_numbers"].empty();

        bool missing = !has_photo_numbers;
        if (has_photo_numbers) {
            const auto defect_id = candidate_id_of(defect);
            for (const auto& photo_number_value : defect["photo_numbers"]) {
                if (!photo_number_value.isString()) {
                    continue;
                }
                if (!photo_number_has_confirmed_link(data, defect_id, photo_number_value.asString())) {
                    missing = true;
                    break;
                }
            }
        }

        if (missing) {
            add_issue(warnings, "defect_without_photo",
                      "已确认病害 " + candidate_id_of(defect) + " 没有已确认且关联的照片。", candidate_id_of(defect));
        }
    }
}

// -----------------------------------------------------------------------
// 警告：不会入库的照片候选
// -----------------------------------------------------------------------

void check_unreferenced_photo_ignored(const Json::Value& data, std::vector<PreflightIssue>& warnings) {
    if (!data["photos"].isArray()) {
        return;
    }
    for (const auto& photo : data["photos"]) {
        const auto status = review_status_of(photo);
        const auto match_status = string_member_or_empty(photo, "match_status");

        const bool will_be_dropped = status == kIgnored || (is_review_settled(status) && match_status == kMatchUnlinked);

        if (will_be_dropped) {
            add_issue(warnings, "unreferenced_photo_ignored",
                      "照片候选 " + candidate_id_of(photo) + " 不会入库（" + (status == kIgnored ? "已忽略" : "未关联") + "）。",
                      candidate_id_of(photo));
        }
    }
}

// -----------------------------------------------------------------------
// 警告：尺寸原文保留但未结构化
// -----------------------------------------------------------------------

void check_measurement_unstructured_kept(const Json::Value& data, std::vector<PreflightIssue>& warnings) {
    if (!data["defects"].isArray()) {
        return;
    }
    for (const auto& defect : data["defects"]) {
        if (!is_review_settled(review_status_of(defect))) {
            continue;
        }

        const bool has_measurement_text = defect.isObject() && defect.isMember("measurement_text")
            && defect["measurement_text"].isString() && !defect["measurement_text"].asString().empty();
        const bool measurements_empty = !defect.isObject() || !defect.isMember("measurements")
            || !defect["measurements"].isArray() || defect["measurements"].empty();

        if (has_measurement_text && measurements_empty) {
            add_issue(warnings, "measurement_unstructured_kept",
                      "病害候选 " + candidate_id_of(defect) + " 的尺寸原文未结构化，将保留原文入库。", candidate_id_of(defect));
        }
    }
}

// -----------------------------------------------------------------------
// 警告：评分分部/部件数量不完整
// -----------------------------------------------------------------------

void check_rating_parts_incomplete(const Json::Value& data, std::vector<PreflightIssue>& warnings) {
    const auto& ratings = data["ratings"];
    if (!ratings.isObject()) {
        add_issue(warnings, "rating_parts_incomplete", "评分对象缺失结构分部与评价部件数据。");
        return;
    }

    const bool structure_parts_ok = ratings["structure_parts"].isArray() && ratings["structure_parts"].size() >= 3;
    const bool evaluation_parts_ok = ratings["evaluation_parts"].isArray() && !ratings["evaluation_parts"].empty();

    if (!structure_parts_ok || !evaluation_parts_ok) {
        add_issue(warnings, "rating_parts_incomplete", "结构分部数量不足 3 个或评价部件为空，评分数据可能不完整。");
    }
}

// 阻断错误与警告的条目结构一致：target_candidate_id 为空字符串时输出 JSON null。
Json::Value issues_to_json(const std::vector<PreflightIssue>& issues) {
    Json::Value json(Json::arrayValue);
    for (const auto& issue : issues) {
        Json::Value item;
        item["code"] = issue.code;
        item["message"] = issue.message;
        item["target_candidate_id"] = issue.target_candidate_id.empty() ? Json::Value(Json::nullValue) : Json::Value(issue.target_candidate_id);
        json.append(item);
    }
    return json;
}

}  // 匿名命名空间

Json::Value PreflightReport::to_json() const {
    Json::Value json;
    json["can_confirm"] = can_confirm;
    json["requires_revision_confirmation"] = requires_revision_confirmation;
    json["blocking_errors"] = issues_to_json(blocking_errors);
    json["warnings"] = issues_to_json(warnings);
    return json;
}

PreflightReport build_preflight_report(const Json::Value& data, const PreflightContext& context) {
    PreflightReport report;
    report.requires_revision_confirmation = context.has_current_annual_facts;

    // 检查 1：导入记录状态。总是执行，与契约是否有效无关。
    check_import_status(context, report.blocking_errors);

    // 检查 2：契约校验。失败时短路返回——契约都不满足时，无法安全定位病害/照片/评分等字段，
    // 继续做检查 3-7 及警告意义不大，且容易因为字段缺失而产生噪声阻断项。
    const auto contract_result = bridge_report::contracts::validate_bridge_annual_inspection_data(data);
    if (!contract_result.ok()) {
        add_issue(report.blocking_errors, "contract_validation_failed", "请求体未通过契约校验：" + contract_result.summary());
        report.can_confirm = report.blocking_errors.empty();
        return report;
    }

    // 契约通过后才继续检查 3-7 与非阻断警告。
    check_import_context_mismatch(data, context, report.blocking_errors);
    check_candidate_pending_review(data, report.blocking_errors);
    check_defect_missing_required_field(data, report.blocking_errors);
    check_photo_link_unresolved(data, report.blocking_errors);
    check_defect_photo_groups(data, report.blocking_errors);
    check_photo_archives(data, report.blocking_errors);
    check_rating_overall_missing(data, report.blocking_errors);

    check_defect_without_photo(data, report.warnings);
    check_unreferenced_photo_ignored(data, report.warnings);
    check_measurement_unstructured_kept(data, report.warnings);
    check_rating_parts_incomplete(data, report.warnings);

    report.can_confirm = report.blocking_errors.empty();
    return report;
}

PreflightContext build_preflight_context(
    const ImportRecordDetail& detail,
    std::optional<int> effective_inspection_year,
    bool has_current_annual_facts
) {
    PreflightContext context;
    context.import_status = detail.import_status;
    context.record_system_number = detail.system_number;
    context.bridge_system_number = detail.bridge_system_number;
    context.inspection_year = effective_inspection_year;
    context.has_current_annual_facts = has_current_annual_facts;
    return context;
}

}  // 命名空间 bridge_report::review
