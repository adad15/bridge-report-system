#include <string>

#include <gtest/gtest.h>
#include <json/value.h>

#include "bridge_report/review/ContractCompatibility.hpp"

using bridge_report::review::ContractCompatibility;
using bridge_report::review::normalize_review_contract;

namespace {

Json::Value make_10_data() {
    Json::Value data(Json::objectValue);
    data["contract"]["name"] = "BridgeAnnualInspectionData";
    data["contract"]["version"] = "1.0";
    data["defects"] = Json::Value(Json::arrayValue);
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "defect_0001";
    data["defects"].append(defect);
    return data;
}

}  // namespace

TEST(ContractCompatibilityTest, UpgradesPending10WithoutPersistingConfirmation) {
    auto data = make_10_data();
    for (auto& defect : data["defects"]) {
        defect.removeMember("group_review_status");
        defect.removeMember("confirmed_missing_photo_numbers");
    }

    const auto result = normalize_review_contract(data, "待校对");

    EXPECT_EQ(result.compatibility, ContractCompatibility::Upgraded10);
    EXPECT_EQ(result.data["contract"]["version"].asString(), "1.1");
    EXPECT_EQ(result.data["defects"][0]["group_review_status"].asString(), "待确认");
    EXPECT_TRUE(result.data["defects"][0]["confirmed_missing_photo_numbers"].empty());
    EXPECT_EQ(data["contract"]["version"].asString(), "1.0");
    EXPECT_FALSE(data["defects"][0].isMember("group_review_status"));
}

TEST(ContractCompatibilityTest, MarksConfirmed10AsLegacyReadOnly) {
    const auto result = normalize_review_contract(make_10_data(), "已确认");

    EXPECT_EQ(result.compatibility, ContractCompatibility::LegacyReadOnly);
    EXPECT_EQ(result.data["contract"]["version"].asString(), "1.1");
    EXPECT_EQ(result.data["defects"][0]["group_review_status"].asString(), "待确认");
    EXPECT_TRUE(result.data["defects"][0]["confirmed_missing_photo_numbers"].empty());
}

TEST(ContractCompatibilityTest, MarksCancelled10AsLegacyReadOnly) {
    const auto result = normalize_review_contract(make_10_data(), "已取消");

    EXPECT_EQ(result.compatibility, ContractCompatibility::LegacyReadOnly);
    EXPECT_EQ(result.data["contract"]["version"].asString(), "1.1");
}

TEST(ContractCompatibilityTest, LeavesNative11DataUnchanged) {
    auto data = make_10_data();
    data["contract"]["version"] = "1.1";
    data["defects"][0]["group_review_status"] = "已确认";
    data["defects"][0]["confirmed_missing_photo_numbers"] = Json::Value(Json::arrayValue);

    const auto result = normalize_review_contract(data, "待校对");

    EXPECT_EQ(result.compatibility, ContractCompatibility::Native11);
    EXPECT_EQ(result.data, data);
}

TEST(ContractCompatibilityTest, NamesCompatibilityValuesExactly) {
    EXPECT_EQ(bridge_report::review::contract_compatibility_name(ContractCompatibility::Native11), "native_1_1");
    EXPECT_EQ(bridge_report::review::contract_compatibility_name(ContractCompatibility::Upgraded10), "upgraded_1_0");
    EXPECT_EQ(bridge_report::review::contract_compatibility_name(ContractCompatibility::LegacyReadOnly), "legacy_read_only");
}
