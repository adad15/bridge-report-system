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

TEST(ContractCompatibilityTest, LeavesNative20DataUnchanged) {
    const auto data = make_contract("2.0");
    const auto result = normalize_review_contract(data, "待校对");
    EXPECT_EQ(result.compatibility, ContractCompatibility::Native20);
    EXPECT_EQ(result.data, data);
    EXPECT_EQ(
        bridge_report::review::contract_compatibility_name(result.compatibility),
        "native_2_0");
}

TEST(ContractCompatibilityTest, DoesNotNormalizeLegacyContracts) {
    const auto legacy = make_contract("1.2");
    const auto result = normalize_review_contract(legacy, "已确认");
    EXPECT_EQ(result.compatibility, ContractCompatibility::Native20);
    EXPECT_EQ(result.data, legacy);
    EXPECT_EQ(result.data["contract"]["version"].asString(), "1.2");
}

TEST(ContractCompatibilityTest, StoredContractRequiresReparseUnlessNative20) {
    EXPECT_TRUE(stored_contract_requires_reparse(make_contract("1.2")));
    EXPECT_TRUE(stored_contract_requires_reparse(Json::Value(Json::objectValue)));
    EXPECT_FALSE(stored_contract_requires_reparse(make_contract("2.0")));
}
