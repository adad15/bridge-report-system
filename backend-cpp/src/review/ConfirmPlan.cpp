#include "bridge_report/review/ConfirmPlan.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

#include <json/json.h>

#include "bridge_report/review/JsonAccessors.hpp"

namespace bridge_report::review {

namespace {

std::optional<std::string> optional_string_member(const Json::Value& object, const char* key) {
    if (!object.isObject() || !object.isMember(key) || !object[key].isString()) {
        return std::nullopt;
    }
    return object[key].asString();
}

std::optional<double> optional_double_member(const Json::Value& object, const char* key) {
    if (!object.isObject() || !object.isMember(key) || !object[key].isNumeric()) {
        return std::nullopt;
    }
    return object[key].asDouble();
}

std::optional<int> optional_int_member(const Json::Value& object, const char* key) {
    if (!object.isObject() || !object.isMember(key) || !object[key].isNumeric()) {
        return std::nullopt;
    }
    return object[key].asInt();
}

// -----------------------------------------------------------------------
// 构件 key 归一化：去首尾空白、内部连续空白压缩为单个空格。
// 只处理 ASCII 空格/制表符/回车/换行；不特殊处理全角空格（U+3000）等多字节空白——
// 中文字符的 UTF-8 编码不含这些 ASCII 字节，逐字节扫描不会破坏多字节序列。
// -----------------------------------------------------------------------

bool is_ascii_space(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::string normalize_whitespace(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    bool pending_space = false;
    for (unsigned char ch : input) {
        if (is_ascii_space(ch)) {
            pending_space = true;
            continue;
        }
        if (pending_space && !result.empty()) {
            result.push_back(' ');
        }
        pending_space = false;
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

// 转义单个 key 段：先把字面反斜杠转义成 "\\"，再把字面竖线转义成 "\|"。两步顺序不可颠倒——
// 先转义反斜杠可保证转义引入的反斜杠不会被后续再次转义，从而得到可证明无歧义的编码：拼接后
// 只有作为段分隔符的那些竖线是"裸"竖线，段内的任何 '\' 与 '|' 都带前导反斜杠。这样不同分段
// 方式的构件（如 "a|b"+"c" 与 "a"+"b|c"）不会拼出相同 key 被错误归并。
std::string escape_key_segment(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (char ch : input) {
        if (ch == '\\' || ch == '|') {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    return result;
}

std::string normalized_component_key(
    const std::string& structure_part, const std::string& component_type, const std::string& business_component_code
) {
    return escape_key_segment(normalize_whitespace(structure_part)) + "|"
        + escape_key_segment(normalize_whitespace(component_type)) + "|"
        + escape_key_segment(normalize_whitespace(business_component_code));
}

// -----------------------------------------------------------------------
// "数量" 原文前导整数解析：仅识别原文起始处的连续 ASCII 数字；无数字前缀（如“约三处”）返回 nullopt。
// -----------------------------------------------------------------------

std::optional<double> parse_leading_integer(const std::string& text) {
    std::size_t index = 0;
    while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
        ++index;
    }
    if (index == 0) {
        return std::nullopt;
    }
    try {
        return std::stod(text.substr(0, index));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// 与 ReviewRoutes 草稿保存一致：jsonb/文本列不保留输入格式，紧凑序列化即可。
std::string write_compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder writer_builder;
    writer_builder["indentation"] = "";
    return Json::writeString(writer_builder, value);
}

// -----------------------------------------------------------------------
// 规则 4：尺寸映射
// -----------------------------------------------------------------------

void append_measurements(const Json::Value& defect, DefectPlan& plan) {
    if (defect.isObject() && defect.isMember("measurements") && defect["measurements"].isArray()) {
        for (const auto& item : defect["measurements"]) {
            if (!item.isObject()) {
                continue;
            }
            MeasurementPlan measurement;
            measurement.measurement_type = string_member_or_empty(item, "dimension_type");
            measurement.numeric_value = optional_double_member(item, "value");
            measurement.unit = optional_string_member(item, "unit");
            measurement.raw_text = string_member_or_empty(item, "source_text");
            measurement.is_auto_parsed = true;
            plan.measurements.push_back(std::move(measurement));
        }
    }

    if (plan.measurements.empty()) {
        const auto measurement_text = optional_string_member(defect, "measurement_text");
        if (measurement_text.has_value() && !measurement_text->empty()) {
            MeasurementPlan measurement;
            measurement.measurement_type = "未识别尺寸";
            measurement.raw_text = *measurement_text;
            measurement.is_auto_parsed = false;
            plan.measurements.push_back(std::move(measurement));
        }
    }

    const auto quantity_text = optional_string_member(defect, "quantity_text");
    if (quantity_text.has_value() && !quantity_text->empty()) {
        const bool has_quantity_measurement = std::any_of(
            plan.measurements.begin(), plan.measurements.end(),
            [](const MeasurementPlan& measurement) { return measurement.measurement_type == "数量"; }
        );
        if (!has_quantity_measurement) {
            MeasurementPlan measurement;
            measurement.measurement_type = "数量";
            measurement.numeric_value = parse_leading_integer(*quantity_text);
            measurement.raw_text = *quantity_text;
            measurement.is_auto_parsed = false;
            plan.measurements.push_back(std::move(measurement));
        }
    }
}

// -----------------------------------------------------------------------
// 规则 1-4：构件去重 + 病害映射
// -----------------------------------------------------------------------

void append_components_and_defects(
    const Json::Value& data, ConfirmPlan& plan, std::unordered_set<std::string>& defect_ids_in_plan
) {
    if (!data.isObject() || !data["defects"].isArray()) {
        return;
    }

    std::unordered_set<std::string> seen_component_keys;

    for (const auto& defect : data["defects"]) {
        if (!defect.isObject()) {
            continue;
        }
        const auto status = review_status_of(defect);
        if (!is_review_settled(status) || string_member_or_empty(defect, "group_review_status") != "已确认") {
            continue;
        }

        const auto structure_part = string_member_or_empty(defect, "structure_part");
        const auto component_name = string_member_or_empty(defect, "component_name");
        const auto component_alias = optional_string_member(defect, "component_alias");
        const bool alias_non_empty = component_alias.has_value() && !component_alias->empty();
        const std::string component_type = alias_non_empty ? *component_alias : component_name;
        const std::string& business_component_code = component_name;
        const auto key = normalized_component_key(structure_part, component_type, business_component_code);

        if (seen_component_keys.insert(key).second) {
            ComponentPlan component;
            component.structure_part = structure_part;
            component.component_type = component_type;
            component.business_component_code = business_component_code;
            component.normalized_component_key = key;
            if (alias_non_empty) {
                component.alias_text = component_alias;
            }
            plan.components.push_back(std::move(component));
        }

        DefectPlan defect_plan;
        defect_plan.candidate_id = candidate_id_of(defect);
        defect_plan.component_key = key;
        defect_plan.structure_part = structure_part;
        defect_plan.part_name = component_alias;
        defect_plan.defect_location = string_member_or_empty(defect, "defect_location");
        defect_plan.defect_type = string_member_or_empty(defect, "defect_type");
        defect_plan.defect_description_raw = string_member_or_empty(defect, "defect_description");
        defect_plan.scale = optional_string_member(defect, "severity");

        const auto& source_ref = defect["source_ref"];
        defect_plan.raw_row_text = optional_string_member(source_ref, "raw_row_text");
        defect_plan.source_table_title = optional_string_member(source_ref, "table_title");
        defect_plan.source_table_index = optional_int_member(source_ref, "table_index");
        defect_plan.source_row_number = optional_int_member(source_ref, "row_index");

        defect_plan.extraction_confidence = optional_double_member(defect, "confidence").value_or(0.0);
        defect_plan.review_status = status;
        defect_plan.review_note = optional_string_member(defect, "review_note");

        append_measurements(defect, defect_plan);

        defect_ids_in_plan.insert(defect_plan.candidate_id);
        plan.defects.push_back(std::move(defect_plan));
    }
}

// -----------------------------------------------------------------------
// 规则 5：照片映射
// -----------------------------------------------------------------------

void append_photos(const Json::Value& data, const std::unordered_set<std::string>& defect_ids_in_plan, ConfirmPlan& plan) {
    if (!data.isObject() || !data["photos"].isArray()) {
        return;
    }

    for (const auto& photo : data["photos"]) {
        if (!photo.isObject() || !is_review_settled(review_status_of(photo))
            || string_member_or_empty(photo, "match_status") != "已确认") {
            continue;
        }

        const auto linked_defect_id = optional_string_member(photo, "linked_defect_candidate_id");
        if (!linked_defect_id.has_value() || linked_defect_id->empty()) {
            continue;  // 未关联
        }
        if (defect_ids_in_plan.find(*linked_defect_id) == defect_ids_in_plan.end()) {
            continue;  // 关联的病害未入计划（已忽略/待确认/不存在）
        }

        const auto archive_relative_path = optional_string_member(photo["extracted_file"], "archive_relative_path");
        if (!archive_relative_path.has_value() || archive_relative_path->empty()) {
            continue;
        }

        PhotoPlan photo_plan;
        photo_plan.candidate_id = candidate_id_of(photo);
        photo_plan.defect_candidate_id = *linked_defect_id;
        photo_plan.photo_number = string_member_or_empty(photo, "photo_number");
        photo_plan.photo_title = optional_string_member(photo["extracted_file"], "original_caption");
        photo_plan.archive_relative_path = *archive_relative_path;
        plan.photos.push_back(std::move(photo_plan));
    }
}

// -----------------------------------------------------------------------
// 规则 6/7：评分三层映射 + overall 顶层同步
// -----------------------------------------------------------------------

void append_overall_rating(const Json::Value& ratings, ConfirmPlan& plan) {
    const auto& overall = ratings["overall"];
    const auto status = review_status_of(overall);
    if (!is_review_settled(status)) {
        plan.overall_score = std::nullopt;
        plan.overall_grade.clear();
        return;
    }

    RatingPlan rating;
    rating.rating_level = "全桥";
    rating.structure_part = "全桥";
    rating.rating_item_name = "全桥";
    rating.score = optional_double_member(overall, "total_score");
    rating.grade = optional_string_member(overall, "overall_grade");
    rating.review_status = status;

    plan.overall_score = rating.score;
    plan.overall_grade = rating.grade.value_or(std::string());

    plan.ratings.push_back(std::move(rating));
}

void append_structure_part_ratings(const Json::Value& ratings, ConfirmPlan& plan) {
    if (!ratings.isObject() || !ratings["structure_parts"].isArray()) {
        return;
    }
    for (const auto& part : ratings["structure_parts"]) {
        if (!part.isObject()) {
            continue;
        }
        const auto status = review_status_of(part);
        if (!is_review_settled(status)) {
            continue;
        }
        RatingPlan rating;
        rating.rating_level = "结构分部";
        rating.structure_part = string_member_or_empty(part, "structure_part");
        rating.rating_item_name = rating.structure_part;
        rating.score = optional_double_member(part, "structure_score");
        rating.grade = optional_string_member(part, "grade");
        rating.weight = optional_double_member(part, "weight");
        rating.review_status = status;
        plan.ratings.push_back(std::move(rating));
    }
}

void append_evaluation_part_ratings(const Json::Value& ratings, ConfirmPlan& plan) {
    if (!ratings.isObject() || !ratings["evaluation_parts"].isArray()) {
        return;
    }
    for (const auto& part : ratings["evaluation_parts"]) {
        if (!part.isObject()) {
            continue;
        }
        const auto status = review_status_of(part);
        if (!is_review_settled(status)) {
            continue;
        }
        RatingPlan rating;
        rating.rating_level = "部件";
        rating.structure_part = string_member_or_empty(part, "structure_part");
        rating.rating_item_name = string_member_or_empty(part, "evaluation_part");
        rating.score = optional_double_member(part, "part_score");

        if (part.isMember("score_rows") && part["score_rows"].isArray() && !part["score_rows"].empty()) {
            rating.remarks = write_compact_json(part["score_rows"]);
        }

        rating.review_status = status;
        plan.ratings.push_back(std::move(rating));
    }
}

void append_ratings(const Json::Value& data, ConfirmPlan& plan) {
    if (!data.isObject() || !data["ratings"].isObject()) {
        plan.overall_score = std::nullopt;
        plan.overall_grade.clear();
        return;
    }
    const auto& ratings = data["ratings"];
    append_overall_rating(ratings, plan);
    append_structure_part_ratings(ratings, plan);
    append_evaluation_part_ratings(ratings, plan);
}

}  // 匿名命名空间

ConfirmPlan build_confirm_plan(const Json::Value& data) {
    ConfirmPlan plan;
    std::unordered_set<std::string> defect_ids_in_plan;

    append_components_and_defects(data, plan, defect_ids_in_plan);
    append_photos(data, defect_ids_in_plan, plan);
    append_ratings(data, plan);

    return plan;
}

}  // 命名空间 bridge_report::review
