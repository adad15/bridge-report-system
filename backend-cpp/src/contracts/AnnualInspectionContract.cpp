#include "bridge_report/contracts/AnnualInspectionContract.hpp"

#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include <json/value.h>

namespace bridge_report::contracts {
namespace {

// 统一生成问题路径，让校验错误可以准确指到 JSON 对象成员。
std::string member_path(const std::string& base_path, const std::string& member) {
    if (base_path.empty()) {
        return member;
    }
    return base_path + "." + member;
}

// 数组元素路径沿用 JSONPath 风格，便于定位第几条病害、照片或评分。
std::string indexed_path(const std::string& base_path, Json::ArrayIndex index) {
    return base_path + "[" + std::to_string(index) + "]";
}

// 必填数组必须显式出现，即使为空也要由解析器写出，避免前端区分不了“空”和“漏字段”。
bool require_array_member(const Json::Value& object, const std::string& base_path, const std::string& member, ContractValidationResult& result) {
    const auto path = member_path(base_path, member);
    if (!object.isObject() || !object.isMember(member)) {
        result.add_issue(path, "is required and must be an array");
        return false;
    }
    if (!object[member].isArray()) {
        result.add_issue(path, "must be an array");
        return false;
    }
    return true;
}

// 必填对象同样要求显式出现，保证候选 JSON 的骨架稳定。
bool require_object_member(const Json::Value& object, const std::string& base_path, const std::string& member, ContractValidationResult& result) {
    const auto path = member_path(base_path, member);
    if (!object.isObject() || !object.isMember(member)) {
        result.add_issue(path, "is required and must be an object");
        return false;
    }
    if (!object[member].isObject()) {
        result.add_issue(path, "must be an object");
        return false;
    }
    return true;
}

bool require_non_empty_string(const Json::Value& object, const std::string& path, const std::string& member, ContractValidationResult& result) {
    const auto full_path = member_path(path, member);
    if (!object.isObject() || !object.isMember(member) || !object[member].isString() || object[member].asString().empty()) {
        result.add_issue(full_path, "must be a non-empty string");
        return false;
    }
    return true;
}

void require_enum(const Json::Value& object, const std::string& path, const std::string& member,
                  const std::unordered_set<std::string>& allowed, ContractValidationResult& result) {
    const auto full_path = member_path(path, member);
    if (!object.isObject() || !object.isMember(member) || !object[member].isString()
        || allowed.find(object[member].asString()) == allowed.end()) {
        result.add_issue(full_path, "contains an invalid value");
    }
}

bool is_safe_relative_path(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    const std::filesystem::path path(value);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const auto& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

const std::unordered_set<std::string>& review_statuses() {
    static const std::unordered_set<std::string> values = {"待确认", "已确认", "已修改", "已忽略"};
    return values;
}

// 自动抽取对象必须带 0 到 1 的置信度，供校对工作台排序和提示风险。
void require_confidence(const Json::Value& object, const std::string& base_path, ContractValidationResult& result) {
    const auto path = member_path(base_path, "confidence");
    if (!object.isObject() || !object.isMember("confidence") || !object["confidence"].isNumeric()) {
        result.add_issue(path, "must be a number between 0 and 1");
        return;
    }

    const auto confidence = object["confidence"].asDouble();
    if (confidence < 0.0 || confidence > 1.0) {
        result.add_issue(path, "must be a number between 0 and 1");
    }
}

// C++ 先确认契约名和版本，防止旧解析器输出被当成新契约处理。
void validate_contract_info(const Json::Value& root, ContractValidationResult& result) {
    if (!require_object_member(root, "", "contract", result)) {
        return;
    }

    const auto& contract = root["contract"];
    if (!contract.isMember("name") || !contract["name"].isString() || contract["name"].asString() != "BridgeAnnualInspectionData") {
        result.add_issue("contract.name", "must be BridgeAnnualInspectionData");
    }
    if (!contract.isMember("version") || !contract["version"].isString() || contract["version"].asString() != "1.1") {
        result.add_issue("contract.version", "must be 1.1");
    }
}

// 病害候选的来源、尺寸数组、照片编号数组和警告数组都是校对流程必需字段。
void validate_defect(const Json::Value& defect, const std::string& path, ContractValidationResult& result) {
    if (!defect.isObject()) {
        result.add_issue(path, "must be an object");
        return;
    }

    require_non_empty_string(defect, path, "candidate_id", result);
    require_enum(defect, path, "review_status", review_statuses(), result);
    require_enum(defect, path, "group_review_status", {"待确认", "已确认"}, result);
    require_array_member(defect, path, "measurements", result);
    require_array_member(defect, path, "photo_numbers", result);
    require_array_member(defect, path, "confirmed_missing_photo_numbers", result);
    require_object_member(defect, path, "source_ref", result);
    require_array_member(defect, path, "warnings", result);
    require_confidence(defect, path, result);
}

// 照片候选必须同时保留抽取文件信息和来源证据，后续人工确认后才写正式照片表。
void validate_photo(const Json::Value& photo, const std::string& path, ContractValidationResult& result) {
    if (!photo.isObject()) {
        result.add_issue(path, "must be an object");
        return;
    }

    require_non_empty_string(photo, path, "candidate_id", result);
    require_non_empty_string(photo, path, "photo_number", result);
    require_enum(photo, path, "review_status", review_statuses(), result);
    require_enum(photo, path, "match_status", {"高置信候选", "待校对", "已确认", "未关联"}, result);
    if (require_object_member(photo, path, "extracted_file", result)) {
        const auto& extracted = photo["extracted_file"];
        if (extracted.isMember("archive_relative_path") && !extracted["archive_relative_path"].isNull()) {
            const auto archive_path = member_path(member_path(path, "extracted_file"), "archive_relative_path");
            if (!extracted["archive_relative_path"].isString()
                || !is_safe_relative_path(extracted["archive_relative_path"].asString())) {
                result.add_issue(archive_path, "must be a safe relative path or null");
            }
        }
    }
    require_object_member(photo, path, "source_ref", result);
    require_array_member(photo, path, "warnings", result);
    require_confidence(photo, path, result);
}

// 对比候选由事实入库后生成，这里只校验通用风险字段和置信度。
void validate_comparison_candidate(const Json::Value& candidate, const std::string& path, ContractValidationResult& result) {
    if (!candidate.isObject()) {
        result.add_issue(path, "must be an object");
        return;
    }

    require_array_member(candidate, path, "warnings", result);
    require_confidence(candidate, path, result);
}

void validate_structure_part(const Json::Value& part, const std::string& path, ContractValidationResult& result) {
    if (!part.isObject()) {
        result.add_issue(path, "must be an object");
        return;
    }

    require_confidence(part, path, result);
    require_enum(part, path, "review_status", review_statuses(), result);
}

void validate_evaluation_part(const Json::Value& part, const std::string& path, ContractValidationResult& result) {
    if (!part.isObject()) {
        result.add_issue(path, "must be an object");
        return;
    }

    // 表 4.1-2 中“评价部件”只有评分，等级最小单元是结构分部。
    if (part.isMember("grade")) {
        result.add_issue(member_path(path, "grade"), "is not allowed");
    }
    require_array_member(part, path, "score_rows", result);
    require_object_member(part, path, "source_ref", result);
    require_confidence(part, path, result);
    require_enum(part, path, "review_status", review_statuses(), result);
}

// 第四章评分整体按“全桥、结构分部、评价部件”三层读取，不在这里重新计算评分。
void validate_ratings(const Json::Value& root, ContractValidationResult& result) {
    if (!require_object_member(root, "", "ratings", result)) {
        return;
    }

    const auto& ratings = root["ratings"];
    if (require_object_member(ratings, "ratings", "overall", result)) {
        require_confidence(ratings["overall"], "ratings.overall", result);
        require_enum(ratings["overall"], "ratings.overall", "review_status", review_statuses(), result);
    }

    if (require_array_member(ratings, "ratings", "structure_parts", result)) {
        const auto& structure_parts = ratings["structure_parts"];
        for (Json::ArrayIndex index = 0; index < structure_parts.size(); ++index) {
            validate_structure_part(structure_parts[index], indexed_path("ratings.structure_parts", index), result);
        }
    }

    if (require_array_member(ratings, "ratings", "evaluation_parts", result)) {
        const auto& evaluation_parts = ratings["evaluation_parts"];
        for (Json::ArrayIndex index = 0; index < evaluation_parts.size(); ++index) {
            validate_evaluation_part(evaluation_parts[index], indexed_path("ratings.evaluation_parts", index), result);
        }
    }

    require_array_member(ratings, "ratings", "warnings", result);
}

// 顶层数组固定写出，保证 Python、C++ 和前端看到的是同一份候选 JSON 骨架。
void validate_top_level_arrays(const Json::Value& root, ContractValidationResult& result) {
    require_array_member(root, "", "defects", result);
    require_array_member(root, "", "photos", result);
    require_array_member(root, "", "comparison_candidates", result);
    require_array_member(root, "", "report_text_candidates", result);
    require_array_member(root, "", "warnings", result);
    require_array_member(root, "", "errors", result);
}

// 这些顶层对象是导入任务、桥梁校验和年度信息的最低上下文。
void validate_required_top_level_members(const Json::Value& root, ContractValidationResult& result) {
    require_object_member(root, "", "import_context", result);
    require_object_member(root, "", "bridge_check", result);
    require_object_member(root, "", "inspection", result);
}

void validate_unique_candidate_ids(const Json::Value& values, const std::string& path, ContractValidationResult& result) {
    if (!values.isArray()) {
        return;
    }
    std::unordered_set<std::string> seen;
    for (Json::ArrayIndex index = 0; index < values.size(); ++index) {
        const auto& value = values[index];
        if (!value.isObject() || !value["candidate_id"].isString() || value["candidate_id"].asString().empty()) {
            continue;
        }
        if (!seen.insert(value["candidate_id"].asString()).second) {
            result.add_issue(member_path(indexed_path(path, index), "candidate_id"), "must be unique within the collection");
        }
    }
}

bool array_contains_string(const Json::Value& values, const std::string& expected) {
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

void validate_photo_relations(const Json::Value& root, ContractValidationResult& result) {
    std::unordered_set<std::string> defect_ids;
    if (root["defects"].isArray()) {
        for (const auto& defect : root["defects"]) {
            if (defect["candidate_id"].isString()) {
                defect_ids.insert(defect["candidate_id"].asString());
            }
        }
    }

    if (root["photos"].isArray()) {
        for (Json::ArrayIndex index = 0; index < root["photos"].size(); ++index) {
            const auto& photo = root["photos"][index];
            if (photo["linked_defect_candidate_id"].isString()
                && !photo["linked_defect_candidate_id"].asString().empty()
                && defect_ids.find(photo["linked_defect_candidate_id"].asString()) == defect_ids.end()) {
                result.add_issue(member_path(indexed_path("photos", index), "linked_defect_candidate_id"),
                                 "must reference an existing defect candidate");
            }
        }
    }

    if (!root["defects"].isArray()) {
        return;
    }
    for (Json::ArrayIndex defect_index = 0; defect_index < root["defects"].size(); ++defect_index) {
        const auto& defect = root["defects"][defect_index];
        if (!defect["confirmed_missing_photo_numbers"].isArray()) {
            continue;
        }
        for (Json::ArrayIndex number_index = 0; number_index < defect["confirmed_missing_photo_numbers"].size(); ++number_index) {
            const auto& number_value = defect["confirmed_missing_photo_numbers"][number_index];
            const auto number_path = indexed_path(
                member_path(indexed_path("defects", defect_index), "confirmed_missing_photo_numbers"), number_index);
            if (!number_value.isString() || !array_contains_string(defect["photo_numbers"], number_value.asString())) {
                result.add_issue(number_path, "must also appear in photo_numbers");
                continue;
            }
            for (const auto& photo : root["photos"]) {
                if (photo["photo_number"].isString() && photo["photo_number"].asString() == number_value.asString()
                    && photo["linked_defect_candidate_id"].isString()
                    && photo["linked_defect_candidate_id"].asString() == defect["candidate_id"].asString()) {
                    result.add_issue(number_path, "cannot be confirmed missing while a linked photo candidate exists");
                    break;
                }
            }
        }
    }
}

}  // 匿名命名空间

void ContractValidationResult::add_issue(std::string path, std::string message) {
    issues_.push_back({std::move(path), std::move(message)});
}

bool ContractValidationResult::ok() const noexcept {
    return issues_.empty();
}

const std::vector<ContractValidationIssue>& ContractValidationResult::issues() const noexcept {
    return issues_;
}

std::string ContractValidationResult::summary() const {
    if (issues_.empty()) {
        return "OK";
    }

    std::ostringstream out;
    out << issues_.size() << " contract validation issue(s)";
    for (const auto& issue : issues_) {
        out << "\n" << issue.path << ": " << issue.message;
    }
    return out.str();
}

ContractValidationResult validate_bridge_annual_inspection_data(const Json::Value& root) {
    ContractValidationResult result;

    if (!root.isObject()) {
        result.add_issue("$", "must be an object");
        return result;
    }

    // 先校验骨架，再深入检查候选数组，便于一次返回多条问题而不是遇错即停。
    validate_contract_info(root, result);
    validate_required_top_level_members(root, result);
    validate_top_level_arrays(root, result);
    validate_ratings(root, result);

    if (root["defects"].isArray()) {
        const auto& defects = root["defects"];
        for (Json::ArrayIndex index = 0; index < defects.size(); ++index) {
            validate_defect(defects[index], indexed_path("defects", index), result);
        }
    }

    if (root["photos"].isArray()) {
        const auto& photos = root["photos"];
        for (Json::ArrayIndex index = 0; index < photos.size(); ++index) {
            validate_photo(photos[index], indexed_path("photos", index), result);
        }
    }

    if (root["comparison_candidates"].isArray()) {
        const auto& candidates = root["comparison_candidates"];
        for (Json::ArrayIndex index = 0; index < candidates.size(); ++index) {
            validate_comparison_candidate(candidates[index], indexed_path("comparison_candidates", index), result);
        }
    }

    validate_unique_candidate_ids(root["defects"], "defects", result);
    validate_unique_candidate_ids(root["photos"], "photos", result);
    validate_unique_candidate_ids(root["comparison_candidates"], "comparison_candidates", result);
    validate_photo_relations(root, result);

    return result;
}

}  // 命名空间 bridge_report::contracts
