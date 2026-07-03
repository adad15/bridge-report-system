#include "bridge_report/contracts/AnnualInspectionContract.hpp"

#include <sstream>
#include <string>
#include <utility>

#include <json/value.h>

namespace bridge_report::contracts {
namespace {

std::string member_path(const std::string& base_path, const std::string& member) {
    if (base_path.empty()) {
        return member;
    }
    return base_path + "." + member;
}

std::string indexed_path(const std::string& base_path, Json::ArrayIndex index) {
    return base_path + "[" + std::to_string(index) + "]";
}

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

void validate_contract_info(const Json::Value& root, ContractValidationResult& result) {
    if (!require_object_member(root, "", "contract", result)) {
        return;
    }

    const auto& contract = root["contract"];
    if (!contract.isMember("name") || !contract["name"].isString() || contract["name"].asString() != "BridgeAnnualInspectionData") {
        result.add_issue("contract.name", "must be BridgeAnnualInspectionData");
    }
    if (!contract.isMember("version") || !contract["version"].isString() || contract["version"].asString() != "1.0") {
        result.add_issue("contract.version", "must be 1.0");
    }
}

void validate_defect(const Json::Value& defect, const std::string& path, ContractValidationResult& result) {
    if (!defect.isObject()) {
        result.add_issue(path, "must be an object");
        return;
    }

    require_array_member(defect, path, "measurements", result);
    require_array_member(defect, path, "photo_numbers", result);
    require_object_member(defect, path, "source_ref", result);
    require_array_member(defect, path, "warnings", result);
    require_confidence(defect, path, result);
}

void validate_photo(const Json::Value& photo, const std::string& path, ContractValidationResult& result) {
    if (!photo.isObject()) {
        result.add_issue(path, "must be an object");
        return;
    }

    require_object_member(photo, path, "extracted_file", result);
    require_object_member(photo, path, "source_ref", result);
    require_array_member(photo, path, "warnings", result);
    require_confidence(photo, path, result);
}

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
}

void validate_evaluation_part(const Json::Value& part, const std::string& path, ContractValidationResult& result) {
    if (!part.isObject()) {
        result.add_issue(path, "must be an object");
        return;
    }

    if (part.isMember("grade")) {
        result.add_issue(member_path(path, "grade"), "is not allowed");
    }
    require_array_member(part, path, "score_rows", result);
    require_object_member(part, path, "source_ref", result);
    require_confidence(part, path, result);
}

void validate_ratings(const Json::Value& root, ContractValidationResult& result) {
    if (!require_object_member(root, "", "ratings", result)) {
        return;
    }

    const auto& ratings = root["ratings"];
    if (require_object_member(ratings, "ratings", "overall", result)) {
        require_confidence(ratings["overall"], "ratings.overall", result);
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

void validate_top_level_arrays(const Json::Value& root, ContractValidationResult& result) {
    require_array_member(root, "", "defects", result);
    require_array_member(root, "", "photos", result);
    require_array_member(root, "", "comparison_candidates", result);
    require_array_member(root, "", "report_text_candidates", result);
    require_array_member(root, "", "warnings", result);
    require_array_member(root, "", "errors", result);
}

void validate_required_top_level_members(const Json::Value& root, ContractValidationResult& result) {
    require_object_member(root, "", "import_context", result);
    require_object_member(root, "", "bridge_check", result);
    require_object_member(root, "", "inspection", result);
}

}  // namespace

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

    return result;
}

}  // namespace bridge_report::contracts
