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
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "defect_0001";
    data["defects"].append(defect);
    data["ratings"] = Json::Value(Json::objectValue);
    data["ratings"]["overall"]["total_score"] = 85.61;
    return data;
}

}  // namespace

TEST(ContractCompatibilityTest, LeavesNative20DataUnchanged) {
    auto data = make_contract("2.0");
    data.removeMember("ratings");

    const auto result = normalize_review_contract(data, "待校对");

    EXPECT_EQ(result.compatibility, ContractCompatibility::Native20);
    EXPECT_EQ(result.data, data);
}

TEST(ContractCompatibilityTest, ExplicitLegacy12GateIsEnabledUntilTask18) {
    EXPECT_TRUE(
        bridge_report::review::kLegacyAnnualInspection12ReadEnabled);
}

TEST(ContractCompatibilityTest, MarksPending12AsLegacyPendingReparse) {
    const auto data = make_contract("1.2");

    const auto result = normalize_review_contract(data, "待校对");

    EXPECT_EQ(
        result.compatibility,
        ContractCompatibility::LegacyPendingReparse);
    EXPECT_EQ(result.data, data);
}

TEST(ContractCompatibilityTest, NormalizesPending10ForLegacyReadOnlyDisplay) {
    auto data = make_contract("1.0");

    const auto result = normalize_review_contract(data, "待校对");

    EXPECT_EQ(
        result.compatibility,
        ContractCompatibility::LegacyPendingReparse);
    EXPECT_EQ(result.data["contract"]["version"].asString(), "1.2");
    EXPECT_EQ(
        result.data["defects"][0]["group_review_status"].asString(),
        "待确认");
    EXPECT_TRUE(
        result.data["defects"][0]["confirmed_missing_photo_numbers"].empty());
    EXPECT_TRUE(
        result.data["ratings"]["component_ratings"].isArray());
    EXPECT_EQ(data["contract"]["version"].asString(), "1.0");
}

TEST(ContractCompatibilityTest, MarksTerminalLegacyAsReadOnly) {
    for (const auto* version : {"1.0", "1.1", "1.2"}) {
        const auto result =
            normalize_review_contract(make_contract(version), "已确认");
        EXPECT_EQ(
            result.compatibility,
            ContractCompatibility::LegacyReadOnly);
    }
}

TEST(ContractCompatibilityTest, NamesCompatibilityValuesExactly) {
    EXPECT_EQ(
        bridge_report::review::contract_compatibility_name(
            ContractCompatibility::Native20),
        "native_2_0");
    EXPECT_EQ(
        bridge_report::review::contract_compatibility_name(
            ContractCompatibility::LegacyPendingReparse),
        "legacy_pending_reparse");
    EXPECT_EQ(
        bridge_report::review::contract_compatibility_name(
            ContractCompatibility::LegacyReadOnly),
        "legacy_read_only");
}

TEST(ContractCompatibilityTest, StoredContractRequiresReparseUnlessNative20) {
    EXPECT_TRUE(stored_contract_requires_reparse(make_contract("1.0")));
    EXPECT_TRUE(stored_contract_requires_reparse(make_contract("1.1")));
    EXPECT_TRUE(stored_contract_requires_reparse(make_contract("1.2")));
    EXPECT_TRUE(
        stored_contract_requires_reparse(Json::Value(Json::objectValue)));
    EXPECT_FALSE(stored_contract_requires_reparse(make_contract("2.0")));
}
