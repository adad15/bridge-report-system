#include "bridge_report/resolution/ResolutionHashes.hpp"

#include <memory>
#include <string>

#include <json/json.h>

#include "bridge_report/auth/PasswordHash.hpp"

namespace bridge_report::resolution {
namespace {

std::string compact_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

std::string string_member_or_empty(const Json::Value& object, const char* key) {
    return object.isObject() && object[key].isString() ? object[key].asString()
                                                       : std::string{};
}

// 适用性输入单独成段：两个哈希都要用它，写两遍迟早分叉。
Json::Value applicability_document(const RatingMatchHashInput& input) {
    Json::Value value(Json::objectValue);
    value["rating_tree_version_id"] = input.rating_tree_version_id;
    value["bridge_component_id"] = input.bridge_component_id;
    value["technical_standard_package_id"] = input.technical_standard_package_id;
    value["standard_bridge_type_id"] = input.standard_bridge_type_id;
    value["standard_component_category_id"] = input.standard_component_category_id;
    return value;
}

}  // namespace

RatingMatchHashInput build_rating_match_hash_input(
    const Json::Value& effective_defect,
    const std::string& source_candidate_id,
    const std::string& bridge_component_id,
    const std::string& technical_standard_package_id,
    const std::string& standard_bridge_type_id,
    const std::string& standard_component_category_id,
    const std::string& rating_tree_version_id) {
    RatingMatchHashInput input;
    input.source_candidate_id = source_candidate_id;
    input.bridge_component_id = bridge_component_id;
    input.technical_standard_package_id = technical_standard_package_id;
    input.standard_bridge_type_id = standard_bridge_type_id;
    input.standard_component_category_id = standard_component_category_id;
    input.rating_tree_version_id = rating_tree_version_id;
    // 来源分组/指标身份不能省：解析器先按桥型和类别收窄节点，再按"来源分组 + 来源指标"
    // 成对命中（RatingTreeResolver.cpp:142）。少算其中任何一项，真实匹配输入变了而
    // 哈希仍可能不变。
    input.source_defect_group_id =
        string_member_or_empty(effective_defect, "source_defect_group_id");
    input.source_defect_group_number =
        string_member_or_empty(effective_defect, "source_defect_group_number");
    input.source_defect_indicator_id =
        string_member_or_empty(effective_defect, "source_defect_indicator_id");
    input.source_defect_indicator_number =
        string_member_or_empty(effective_defect, "source_defect_indicator_number");
    input.defect_type = string_member_or_empty(effective_defect, "defect_type");
    input.defect_location = string_member_or_empty(effective_defect, "defect_location");
    input.defect_description =
        string_member_or_empty(effective_defect, "defect_description");
    return input;
}

RatingMatchHashes compute_rating_match_hashes(const RatingMatchHashInput& input) {
    RatingMatchHashes hashes;

    const auto applicability = applicability_document(input);
    hashes.applicability_hash = "sha256:" + auth::sha256_hex(compact_json(applicability));

    Json::Value match_input(Json::objectValue);
    match_input["applicability"] = applicability;
    match_input["source_candidate_id"] = input.source_candidate_id;
    match_input["source_defect_group_id"] = input.source_defect_group_id;
    match_input["source_defect_group_number"] = input.source_defect_group_number;
    match_input["source_defect_indicator_id"] = input.source_defect_indicator_id;
    match_input["source_defect_indicator_number"] = input.source_defect_indicator_number;
    match_input["defect_type"] = input.defect_type;
    match_input["defect_location"] = input.defect_location;
    match_input["defect_description"] = input.defect_description;
    // defect_scale 有意不进哈希：标度变化不重新选择评分树节点（§19.3），与现行一致。
    hashes.match_input_hash = "sha256:" + auth::sha256_hex(compact_json(match_input));

    return hashes;
}

}  // namespace bridge_report::resolution
