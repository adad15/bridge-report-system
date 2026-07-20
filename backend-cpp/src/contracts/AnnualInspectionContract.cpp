#include "bridge_report/contracts/AnnualInspectionContract.hpp"

#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include <json/value.h>

namespace bridge_report::contracts {
namespace {

std::string member_path(const std::string& base_path, const std::string& member) {
    return base_path.empty() ? member : base_path + "." + member;
}

std::string indexed_path(const std::string& base_path, Json::ArrayIndex index) {
    return base_path + "[" + std::to_string(index) + "]";
}

bool require_array_member(
    const Json::Value& object,
    const std::string& base_path,
    const std::string& member,
    ContractValidationResult& result) {
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

bool require_object_member(
    const Json::Value& object,
    const std::string& base_path,
    const std::string& member,
    ContractValidationResult& result) {
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

bool require_non_empty_string(
    const Json::Value& object,
    const std::string& path,
    const std::string& member,
    ContractValidationResult& result) {
    const auto full_path = member_path(path, member);
    if (!object.isObject() || !object.isMember(member) || !object[member].isString() ||
        object[member].asString().empty()) {
        result.add_issue(full_path, "must be a non-empty string");
        return false;
    }
    return true;
}

void require_enum(
    const Json::Value& object,
    const std::string& path,
    const std::string& member,
    const std::unordered_set<std::string>& allowed,
    ContractValidationResult& result) {
    const auto full_path = member_path(path, member);
    if (!object.isObject() || !object.isMember(member) || !object[member].isString() ||
        !allowed.contains(object[member].asString())) {
        result.add_issue(full_path, "contains an invalid value");
    }
}

void require_optional_nullable_string(
    const Json::Value& object,
    const std::string& path,
    const std::string& member,
    ContractValidationResult& result) {
    if (!object.isObject() || !object.isMember(member) || object[member].isNull()) {
        return;
    }
    if (!object[member].isString()) {
        result.add_issue(member_path(path, member), "must be a string or null");
    }
}

void validate_optional_string_array(
    const Json::Value& object,
    const std::string& path,
    const std::string& member,
    ContractValidationResult& result) {
    if (!object.isObject() || !object.isMember(member)) return;
    const auto& values = object[member];
    if (!values.isArray()) {
        result.add_issue(member_path(path, member), "must be an array");
        return;
    }
    std::unordered_set<std::string> unique;
    for (Json::ArrayIndex index = 0; index < values.size(); ++index) {
        if (!values[index].isString()) {
            result.add_issue(indexed_path(member_path(path, member), index), "must be a string");
        } else if (!unique.insert(values[index].asString()).second) {
            result.add_issue(member_path(path, member), "must contain unique values");
        }
    }
}

void require_optional_positive_integer(
    const Json::Value& object,
    const std::string& path,
    const std::string& member,
    ContractValidationResult& result) {
    if (!object.isObject() || !object.isMember(member) || object[member].isNull()) {
        return;
    }
    if (!object[member].isIntegral() || object[member].asInt64() <= 0) {
        result.add_issue(member_path(path, member), "must be a positive integer or null");
    }
}

void require_confidence(
    const Json::Value& object,
    const std::string& base_path,
    ContractValidationResult& result) {
    const auto path = member_path(base_path, "confidence");
    if (!object.isObject() || !object.isMember("confidence") ||
        !object["confidence"].isNumeric()) {
        result.add_issue(path, "must be a number between 0 and 1");
        return;
    }
    const auto confidence = object["confidence"].asDouble();
    if (confidence < 0.0 || confidence > 1.0) {
        result.add_issue(path, "must be a number between 0 and 1");
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
    static const std::unordered_set<std::string> values = {
        "待确认", "已确认", "已修改", "已忽略"};
    return values;
}

const std::unordered_set<std::string>& structure_parts() {
    static const std::unordered_set<std::string> values = {
        "全桥", "上部结构", "下部结构", "桥面系", "其他"};
    return values;
}

void validate_contract_info(
    const Json::Value& root,
    ContractValidationResult& result) {
    if (!require_object_member(root, "", "contract", result)) {
        return;
    }
    const auto& contract = root["contract"];
    if (!contract["name"].isString() ||
        contract["name"].asString() != "BridgeAnnualInspectionData") {
        result.add_issue("contract.name", "must be BridgeAnnualInspectionData");
    }
    const auto version = contract["version"].isString()
                             ? contract["version"].asString()
                             : std::string{};
    if (version != "2.0") {
        result.add_issue("contract.version", "must be 2.0");
    }
}

void validate_source_ref(
    const Json::Value& object,
    const std::string& path,
    ContractValidationResult& result) {
    if (!require_object_member(object, path, "source_ref", result)) {
        return;
    }
    const auto& source_ref = object["source_ref"];
    if (source_ref.isMember("source_type")) {
        require_enum(
            source_ref,
            member_path(path, "source_ref"),
            "source_type",
            {"word", "manual"},
            result);
    }
}

void reject_member(
    const Json::Value& object,
    const std::string& path,
    const std::string& member,
    ContractValidationResult& result) {
    if (object.isObject() && object.isMember(member)) {
        result.add_issue(member_path(path, member), "is not allowed in contract 2.0");
    }
}

void validate_measurement(
    const Json::Value& measurement,
    const std::string& path,
    ContractValidationResult& result) {
    if (!measurement.isObject()) {
        result.add_issue(path, "must be an object");
        return;
    }
    require_non_empty_string(measurement, path, "dimension_type", result);
    require_non_empty_string(measurement, path, "unit", result);
    require_non_empty_string(measurement, path, "source_text", result);
    require_enum(measurement, path, "value_type", {"single", "range"}, result);
    if (!measurement.isMember("is_approximate") || !measurement["is_approximate"].isBool()) {
        result.add_issue(member_path(path, "is_approximate"), "must be a boolean");
    }

    const auto value_type = measurement["value_type"].isString()
        ? measurement["value_type"].asString()
        : std::string{};
    const bool value_is_number = measurement.isMember("value") && measurement["value"].isNumeric();
    const bool value_is_null = measurement.isMember("value") && measurement["value"].isNull();
    const bool minimum_is_number = measurement.isMember("minimum_value") && measurement["minimum_value"].isNumeric();
    const bool minimum_is_null = measurement.isMember("minimum_value") && measurement["minimum_value"].isNull();
    const bool maximum_is_number = measurement.isMember("maximum_value") && measurement["maximum_value"].isNumeric();
    const bool maximum_is_null = measurement.isMember("maximum_value") && measurement["maximum_value"].isNull();

    if (value_type == "single") {
        if (!value_is_number || !minimum_is_null || !maximum_is_null) {
            result.add_issue(path, "single measurement requires value and null range endpoints");
        }
    } else if (value_type == "range") {
        if (!value_is_null || !minimum_is_number || !maximum_is_number) {
            result.add_issue(path, "range measurement requires null value and numeric endpoints");
        } else if (measurement["minimum_value"].asDouble() > measurement["maximum_value"].asDouble()) {
            result.add_issue(path, "range minimum_value must not exceed maximum_value");
        }
    }
}

void validate_defect(
    const Json::Value& defect,
    const std::string& path,
    ContractValidationResult& result) {
    if (!defect.isObject()) {
        result.add_issue(path, "must be an object");
        return;
    }

    require_non_empty_string(defect, path, "candidate_id", result);
    require_non_empty_string(defect, path, "component_name", result);
    require_non_empty_string(defect, path, "defect_type", result);
    require_non_empty_string(defect, path, "defect_location", result);
    require_non_empty_string(defect, path, "defect_description", result);
    require_enum(defect, path, "review_status", review_statuses(), result);
    require_enum(defect, path, "group_review_status", {"待确认", "已确认"}, result);
    require_optional_positive_integer(defect, path, "defect_scale", result);
    require_optional_nullable_string(defect, path, "component_number", result);
    require_optional_nullable_string(defect, path, "bridge_component_id", result);
    require_optional_nullable_string(
        defect, path, "standard_component_category_id", result);
    require_optional_nullable_string(
        defect, path, "component_inventory_revision_id", result);
    require_optional_nullable_string(
        defect, path, "component_match_confirmed_by", result);
    validate_optional_string_array(
        defect, path, "component_match_candidate_ids", result);
    if (defect.isMember("component_match_method") &&
        !defect["component_match_method"].isNull()) {
        require_enum(
            defect,
            path,
            "component_match_method",
            {"exact", "confirmed_alias", "normalized_candidate", "manual"},
            result);
    }
    if (defect.isMember("source_structure_part") &&
        !defect["source_structure_part"].isNull()) {
        require_enum(
            defect, path, "source_structure_part", structure_parts(), result);
    }
    if (defect.isMember("resolved_structure_part") &&
        !defect["resolved_structure_part"].isNull()) {
        require_enum(
            defect, path, "resolved_structure_part", structure_parts(), result);
    }

    reject_member(defect, path, "structure_part", result);
    reject_member(defect, path, "component_alias", result);
    reject_member(defect, path, "defect_deduction", result);

    if (require_array_member(defect, path, "measurements", result)) {
        for (Json::ArrayIndex index = 0; index < defect["measurements"].size(); ++index) {
            validate_measurement(
                defect["measurements"][index],
                indexed_path(member_path(path, "measurements"), index),
                result);
        }
    }
    require_array_member(defect, path, "photo_numbers", result);
    require_array_member(defect, path, "confirmed_missing_photo_numbers", result);
    validate_source_ref(defect, path, result);
    require_array_member(defect, path, "warnings", result);
    require_confidence(defect, path, result);
}

void validate_photo(
    const Json::Value& photo,
    const std::string& path,
    ContractValidationResult& result) {
    if (!photo.isObject()) {
        result.add_issue(path, "must be an object");
        return;
    }
    require_non_empty_string(photo, path, "candidate_id", result);
    require_non_empty_string(photo, path, "photo_number", result);
    require_enum(photo, path, "review_status", review_statuses(), result);
    require_enum(
        photo,
        path,
        "match_status",
        {"高置信候选", "待校对", "已确认", "未关联", "已忽略"},
        result);
    if (require_object_member(photo, path, "extracted_file", result)) {
        const auto& extracted = photo["extracted_file"];
        if (extracted.isMember("archive_relative_path") &&
            !extracted["archive_relative_path"].isNull()) {
            const auto archive_path =
                member_path(member_path(path, "extracted_file"), "archive_relative_path");
            if (!extracted["archive_relative_path"].isString() ||
                !is_safe_relative_path(extracted["archive_relative_path"].asString())) {
                result.add_issue(
                    archive_path, "must be a safe relative path or null");
            }
        }
    }
    validate_source_ref(photo, path, result);
    require_array_member(photo, path, "warnings", result);
    require_confidence(photo, path, result);
}

void validate_comparison_candidate(
    const Json::Value& candidate,
    const std::string& path,
    ContractValidationResult& result) {
    if (!candidate.isObject()) {
        result.add_issue(path, "must be an object");
        return;
    }
    require_array_member(candidate, path, "warnings", result);
    require_confidence(candidate, path, result);
}

void validate_unique_candidate_ids(
    const Json::Value& values,
    const std::string& path,
    ContractValidationResult& result) {
    if (!values.isArray()) {
        return;
    }
    std::unordered_set<std::string> seen;
    for (Json::ArrayIndex index = 0; index < values.size(); ++index) {
        const auto& value = values[index];
        if (!value.isObject() || !value["candidate_id"].isString() ||
            value["candidate_id"].asString().empty()) {
            continue;
        }
        if (!seen.insert(value["candidate_id"].asString()).second) {
            result.add_issue(
                member_path(indexed_path(path, index), "candidate_id"),
                "must be unique within the collection");
        }
    }
}

bool array_contains_string(
    const Json::Value& values,
    const std::string& expected) {
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

void validate_photo_relations(
    const Json::Value& root,
    ContractValidationResult& result) {
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
            if (photo["linked_defect_candidate_id"].isString() &&
                !photo["linked_defect_candidate_id"].asString().empty() &&
                !defect_ids.contains(
                    photo["linked_defect_candidate_id"].asString())) {
                result.add_issue(
                    member_path(
                        indexed_path("photos", index),
                        "linked_defect_candidate_id"),
                    "must reference an existing defect candidate");
            }
        }
    }

    if (!root["defects"].isArray()) {
        return;
    }
    for (Json::ArrayIndex defect_index = 0;
         defect_index < root["defects"].size();
         ++defect_index) {
        const auto& defect = root["defects"][defect_index];
        if (!defect["confirmed_missing_photo_numbers"].isArray()) {
            continue;
        }
        for (Json::ArrayIndex number_index = 0;
             number_index < defect["confirmed_missing_photo_numbers"].size();
             ++number_index) {
            const auto& number_value =
                defect["confirmed_missing_photo_numbers"][number_index];
            const auto number_path = indexed_path(
                member_path(
                    indexed_path("defects", defect_index),
                    "confirmed_missing_photo_numbers"),
                number_index);
            if (!number_value.isString() ||
                !array_contains_string(
                    defect["photo_numbers"], number_value.asString())) {
                result.add_issue(
                    number_path, "must also appear in photo_numbers");
                continue;
            }
            for (const auto& photo : root["photos"]) {
                if (photo["photo_number"].isString() &&
                    photo["photo_number"].asString() == number_value.asString() &&
                    photo["linked_defect_candidate_id"].isString() &&
                    photo["linked_defect_candidate_id"].asString() ==
                        defect["candidate_id"].asString()) {
                    result.add_issue(
                        number_path,
                        "cannot be confirmed missing while a linked photo candidate exists");
                    break;
                }
            }
        }
    }
}

}  // namespace

void ContractValidationResult::add_issue(std::string path, std::string message) {
    issues_.push_back({std::move(path), std::move(message)});
}

bool ContractValidationResult::ok() const noexcept {
    return issues_.empty();
}

const std::vector<ContractValidationIssue>&
ContractValidationResult::issues() const noexcept {
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

ContractValidationResult validate_bridge_annual_inspection_data(
    const Json::Value& root) {
    ContractValidationResult result;
    if (!root.isObject()) {
        result.add_issue("$", "must be an object");
        return result;
    }

    validate_contract_info(root, result);
    require_object_member(root, "", "import_context", result);
    require_object_member(root, "", "bridge_check", result);
    require_object_member(root, "", "inspection", result);
    require_array_member(root, "", "defects", result);
    require_array_member(root, "", "photos", result);
    require_array_member(root, "", "comparison_candidates", result);
    require_array_member(root, "", "report_text_candidates", result);
    require_array_member(root, "", "warnings", result);
    require_array_member(root, "", "errors", result);
    reject_member(root, "", "ratings", result);

    if (root["defects"].isArray()) {
        for (Json::ArrayIndex index = 0; index < root["defects"].size(); ++index) {
            validate_defect(
                root["defects"][index], indexed_path("defects", index), result);
        }
    }
    if (root["photos"].isArray()) {
        for (Json::ArrayIndex index = 0; index < root["photos"].size(); ++index) {
            validate_photo(
                root["photos"][index], indexed_path("photos", index), result);
        }
    }
    if (root["comparison_candidates"].isArray()) {
        for (Json::ArrayIndex index = 0;
             index < root["comparison_candidates"].size();
             ++index) {
            validate_comparison_candidate(
                root["comparison_candidates"][index],
                indexed_path("comparison_candidates", index),
                result);
        }
    }

    validate_unique_candidate_ids(root["defects"], "defects", result);
    validate_unique_candidate_ids(root["photos"], "photos", result);
    validate_unique_candidate_ids(
        root["comparison_candidates"], "comparison_candidates", result);
    validate_photo_relations(root, result);
    return result;
}

}  // namespace bridge_report::contracts
