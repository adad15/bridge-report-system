#include "bridge_report/review/PreflightReport.hpp"

#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <vector>

#include "bridge_report/contracts/AnnualInspectionContract.hpp"
#include "bridge_report/review/ComponentScore.hpp"
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

std::string score_text(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
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
        if (ratings["component_ratings"].isArray()) {
            for (const auto& rating : ratings["component_ratings"]) {
                if (review_status_of(rating) == kPending) {
                    add_issue(blocking, "candidate_pending_review",
                              "构件评分候选 " + candidate_id_of(rating) + " 仍处于待确认状态。", candidate_id_of(rating));
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
// 检查 6b：构件评分独立复算与差异处理（合同 1.2）
// -----------------------------------------------------------------------

std::optional<double> optional_numeric_member(const Json::Value& object, const char* key) {
    if (!object.isObject() || !object.isMember(key) || object[key].isNull() || !object[key].isNumeric()) {
        return std::nullopt;
    }
    return object[key].asDouble();
}

// 病害是否属于该构件评分的构件（component_ref 三字段原文相等；空 alias 与缺省等价）。
bool defect_matches_component_ref(const Json::Value& defect, const Json::Value& component_ref) {
    if (string_member_or_empty(defect, "structure_part") != string_member_or_empty(component_ref, "structure_part")) {
        return false;
    }
    if (string_member_or_empty(defect, "component_name") != string_member_or_empty(component_ref, "component_name")) {
        return false;
    }
    return string_member_or_empty(defect, "component_alias")
        == string_member_or_empty(component_ref, "component_alias");
}

void check_component_ratings(const Json::Value& data, std::vector<PreflightIssue>& blocking) {
    const auto& ratings = data["ratings"];
    if (!ratings.isObject() || !ratings["component_ratings"].isArray()) {
        return;
    }

    std::vector<std::string> seen_component_refs;
    for (const auto& rating : ratings["component_ratings"]) {
        const auto status = review_status_of(rating);
        if (!is_review_settled(status)) {
            continue;
        }
        const auto rating_id = candidate_id_of(rating);
        const auto& component_ref = rating["component_ref"];
        const auto validation_status = string_member_or_empty(rating, "score_validation_status");

        // 同一构件只允许一条已定评分，防止把组评分复制成多个正式评分事实。
        const auto ref_key = string_member_or_empty(component_ref, "structure_part") + "|"
            + string_member_or_empty(component_ref, "component_name") + "|"
            + string_member_or_empty(component_ref, "component_alias");
        for (const auto& seen : seen_component_refs) {
            if (seen == ref_key) {
                add_issue(blocking, "component_rating_duplicate_component",
                          "构件评分候选 " + rating_id + " 与另一条候选指向同一构件。", rating_id);
                break;
            }
        }
        seen_component_refs.push_back(ref_key);

        // 未解决的差异不允许入库：必须显式选择最终分并填写原因。
        if (validation_status == "不一致" || validation_status == "无法复算") {
            add_issue(blocking, "component_score_pending_resolution",
                      "构件评分候选 " + rating_id + " 校验状态为「" + validation_status
                          + "」，必须显式选择最终分并填写原因。",
                      rating_id);
        }

        // 证据链校验：引用的病害必须存在且未被忽略。
        std::vector<double> deductions;
        bool references_valid = true;
        bool deductions_complete = true;
        if (rating["deduction_defect_candidate_ids"].isArray()) {
            for (const auto& id_value : rating["deduction_defect_candidate_ids"]) {
                const auto defect_id = id_value.isString() ? id_value.asString() : std::string();
                const auto* defect = find_defect_by_candidate_id(data, defect_id);
                if (defect == nullptr || review_status_of(*defect) == kIgnored) {
                    add_issue(blocking, "component_score_defect_reference_invalid",
                              "构件评分候选 " + rating_id + " 引用的病害 " + defect_id + " 不存在或已忽略。",
                              rating_id);
                    references_valid = false;
                    continue;
                }
                const auto deduction = optional_numeric_member(*defect, "defect_deduction");
                if (deduction.has_value()) {
                    deductions.push_back(*deduction);
                } else {
                    deductions_complete = false;
                }
            }
        }

        // 完整性校验：同构件还有带扣分且未忽略的已定病害没进证据链，说明编辑后未重算。
        if (data["defects"].isArray() && component_ref.isObject()) {
            for (const auto& defect : data["defects"]) {
                const auto defect_status = review_status_of(defect);
                if (defect_status == kIgnored || !is_review_settled(defect_status)) {
                    continue;
                }
                if (!defect_matches_component_ref(defect, component_ref)) {
                    continue;
                }
                if (!optional_numeric_member(defect, "defect_deduction").has_value()) {
                    continue;
                }
                const auto defect_id = candidate_id_of(defect);
                if (!rating["deduction_defect_candidate_ids"].isArray()
                    || !string_array_contains(rating["deduction_defect_candidate_ids"], defect_id)) {
                    add_issue(blocking, "component_score_deduction_incomplete",
                              "构件评分候选 " + rating_id + " 未纳入同构件病害 " + defect_id + " 的扣分。",
                              rating_id);
                }
            }
        }

        // 独立复算：C++ 在入库前重跑同一纯函数，不信任 Python 或前端结果。
        // 扣分缺失或含 [0,100] 之外的非法值时纯函数无结果；0 是合法扣分。
        if (!references_valid) {
            continue;  // 引用已阻断，复算无意义。
        }
        const auto recalculated = (deductions_complete && !deductions.empty())
            ? compute_component_score(deductions)
            : std::nullopt;
        const auto calculated = optional_numeric_member(rating, "calculated_score");
        const auto confirmed = optional_numeric_member(rating, "confirmed_score");
        const bool stored_matches_recalc = recalculated.has_value() && calculated.has_value()
            && std::abs(recalculated->score - *calculated) <= 1e-6;
        const auto require_confirmed_score = [&](double expected, const std::string& choice) {
            const auto rounded_expected = round_score_to_two_decimals(expected);
            if (!confirmed.has_value()
                || round_score_to_two_decimals(*confirmed) != rounded_expected) {
                const auto current = confirmed.has_value() ? score_text(*confirmed) : std::string("空");
                add_issue(
                    blocking,
                    "component_score_confirmed_value_mismatch",
                    "构件评分候选 " + rating_id + " 选择了「" + choice + "」，最终分应为 "
                        + score_text(rounded_expected) + "，当前为 " + current + "。",
                    rating_id
                );
            }
        };

        if (validation_status == "无法复算") {
            if (recalculated.has_value()) {
                add_issue(blocking, "component_score_recalc_mismatch",
                          "构件评分候选 " + rating_id + " 标记为无法复算，但扣分齐全且可复算，请重新校对。",
                          rating_id);
            }
            continue;
        }
        if (validation_status == "一致" || validation_status == "不一致") {
            if (!recalculated.has_value()) {
                add_issue(blocking, "component_score_recalc_mismatch",
                          "构件评分候选 " + rating_id + " 声称已复算，但扣分不齐全或不可复算，请重新校对。",
                          rating_id);
                continue;
            }
            if (!stored_matches_recalc) {
                add_issue(blocking, "component_score_recalc_mismatch",
                          "构件评分候选 " + rating_id + " 的复算分与后端独立复算结果不一致。", rating_id);
                continue;
            }
            // 自动状态与两位小数比较结论必须吻合。
            const auto source = optional_numeric_member(rating, "source_score");
            const auto expected_auto = classify_score_validation(source, recalculated->score);
            if ((validation_status == "一致" && expected_auto != "一致")
                || (validation_status == "不一致" && expected_auto == "一致")) {
                add_issue(blocking, "component_score_recalc_mismatch",
                          "构件评分候选 " + rating_id + " 的校验状态与来源分/复算分两位小数比较结论不符。",
                          rating_id);
            }
            if (validation_status == "一致" && source.has_value() && expected_auto == "一致") {
                require_confirmed_score(*source, "一致");
            }
            continue;
        }
        if (validation_status == "人工采用复算值") {
            // 采用复算值必须建立在可复算且与后端独立复算一致的基础上。
            if (!stored_matches_recalc) {
                add_issue(blocking, "component_score_recalc_mismatch",
                          "构件评分候选 " + rating_id + " 采用复算值，但复算依据与后端独立复算不一致。", rating_id);
            }
            if (recalculated.has_value()) {
                require_confirmed_score(recalculated->score, "人工采用复算值");
            }
            continue;
        }
        // 人工接受Word值：合法来源是"不一致"（复算分存在且与独立复算一致）或
        // "无法复算"（复算分为空且确实不可复算）；证据链变化后仍挂旧状态则拦截。
        if (recalculated.has_value()) {
            if (!calculated.has_value() || !stored_matches_recalc) {
                add_issue(blocking, "component_score_recalc_mismatch",
                          "构件评分候选 " + rating_id + " 的扣分证据已可复算，请重新校对后再处理差异。", rating_id);
            }
        } else if (calculated.has_value()) {
            add_issue(blocking, "component_score_recalc_mismatch",
                      "构件评分候选 " + rating_id + " 记录了复算分但扣分证据已不可复算，请重新校对。", rating_id);
        }
        const auto source = optional_numeric_member(rating, "source_score");
        if (!source.has_value()) {
            add_issue(blocking, "component_score_confirmed_value_mismatch",
                      "构件评分候选 " + rating_id + " 选择了「人工接受Word值」，但 Word 来源分为空。", rating_id);
        } else {
            require_confirmed_score(*source, "人工接受Word值");
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
    const auto mode = data["contract"]["version"].isString() &&
                              data["contract"]["version"].asString() == "1.2"
                          ? bridge_report::contracts::AnnualInspectionValidationMode::Legacy12Transition
                          : bridge_report::contracts::AnnualInspectionValidationMode::FinalVersion2;
    const auto contract_result =
        bridge_report::contracts::validate_bridge_annual_inspection_data(data, mode);
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
    check_component_ratings(data, report.blocking_errors);
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
