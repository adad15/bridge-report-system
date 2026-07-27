#include <string>

#include <gtest/gtest.h>
#include <json/value.h>

#include "bridge_report/review/ContractCompatibility.hpp"

using bridge_report::review::ContractCompatibility;
using bridge_report::review::normalize_review_contract;
using bridge_report::review::stored_contract_requires_reparse;

namespace {

Json::Value make_contract(const std::string& version) {
    Json::Value data(Json::objectValue);
    data["contract"]["name"] = "BridgeAnnualInspectionData";
    data["contract"]["version"] = version;
    data["defects"] = Json::Value(Json::arrayValue);
    data["photos"] = Json::Value(Json::arrayValue);
    return data;
}

}  // namespace

TEST(ContractCompatibilityTest, LeavesNative30DataUnchanged) {
    const auto data = make_contract("3.0");
    const auto result = normalize_review_contract(data, "待校对");
    EXPECT_EQ(result.compatibility, ContractCompatibility::Native30);
    EXPECT_EQ(result.data, data);
    EXPECT_EQ(
        bridge_report::review::contract_compatibility_name(result.compatibility),
        "native_3_0");
}

TEST(ContractCompatibilityTest, DoesNotNormalizeLegacyContracts) {
    const auto legacy = make_contract("1.2");
    const auto result = normalize_review_contract(legacy, "已确认");
    EXPECT_EQ(result.compatibility, ContractCompatibility::Native30);
    EXPECT_EQ(result.data, legacy);
    EXPECT_EQ(result.data["contract"]["version"].asString(), "1.2");
}

TEST(ContractCompatibilityTest, StoredContractRequiresReparseUnlessNative30) {
    EXPECT_TRUE(stored_contract_requires_reparse(make_contract("1.2")));
    EXPECT_TRUE(stored_contract_requires_reparse(make_contract("2.0")));
    EXPECT_TRUE(stored_contract_requires_reparse(Json::Value(Json::objectValue)));
    EXPECT_FALSE(stored_contract_requires_reparse(make_contract("3.0")));
}

TEST(ContractCompatibilityTest, RemovesOnlyStaleMatchWarningsFromResolvedDefect) {
    auto data = make_contract("3.0");
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "d1";
    defect["bridge_component_id"] = "component-1";
    defect["warnings"] = Json::Value(Json::arrayValue);
    for (const auto& code : {"defect_component_match_required", "defect_scale_invalid"}) {
        Json::Value warning(Json::objectValue);
        warning["code"] = code;
        warning["message"] = code;
        warning["severity"] = "warning";
        defect["warnings"].append(std::move(warning));
    }
    data["defects"].append(defect);

    const auto result = normalize_review_contract(data, "待校对");
    ASSERT_EQ(result.data["defects"][0]["warnings"].size(), 1u);
    EXPECT_EQ(
        result.data["defects"][0]["warnings"][0]["code"].asString(),
        "defect_scale_invalid");
}

TEST(ContractCompatibilityTest, RestoresOneAppropriateWarningForUnresolvedDefect) {
    auto data = make_contract("3.0");
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "d1";
    defect["bridge_component_id"] = Json::Value();
    defect["component_match_candidate_ids"].append("component-1");
    defect["warnings"] = Json::Value(Json::arrayValue);
    data["defects"].append(defect);

    const auto result = normalize_review_contract(data, "待校对");
    ASSERT_EQ(result.data["defects"][0]["warnings"].size(), 1u);
    EXPECT_EQ(
        result.data["defects"][0]["warnings"][0]["code"].asString(),
        "defect_component_match_ambiguous");
}
