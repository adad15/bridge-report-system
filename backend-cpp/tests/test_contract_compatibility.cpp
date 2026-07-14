#include <string>

#include <gtest/gtest.h>
#include <json/value.h>

#include "bridge_report/review/ContractCompatibility.hpp"

using bridge_report::review::ContractCompatibility;
using bridge_report::review::normalize_review_contract;
using bridge_report::review::stored_contract_requires_reparse;

namespace {

Json::Value make_legacy_data(const std::string& version) {
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

TEST(ContractCompatibilityTest, MarksPending10AsLegacyPendingReparseWithDisplayNormalization) {
    auto data = make_legacy_data("1.0");
    for (auto& defect : data["defects"]) {
        defect.removeMember("group_review_status");
        defect.removeMember("confirmed_missing_photo_numbers");
    }

    const auto result = normalize_review_contract(data, "待校对");

    EXPECT_EQ(result.compatibility, ContractCompatibility::LegacyPendingReparse);
    EXPECT_EQ(result.data["contract"]["version"].asString(), "1.2");
    EXPECT_EQ(result.data["defects"][0]["group_review_status"].asString(), "待确认");
    EXPECT_TRUE(result.data["defects"][0]["confirmed_missing_photo_numbers"].empty());
    EXPECT_TRUE(result.data["ratings"]["component_ratings"].isArray());
    EXPECT_TRUE(result.data["ratings"]["component_ratings"].empty());
    // 展示规范化只作用于返回克隆，原始数据（即存量 JSON）保持旧版本。
    EXPECT_EQ(data["contract"]["version"].asString(), "1.0");
    EXPECT_FALSE(data["defects"][0].isMember("group_review_status"));
    EXPECT_FALSE(data["ratings"].isMember("component_ratings"));
}

TEST(ContractCompatibilityTest, MarksPending11AsLegacyPendingReparse) {
    const auto result = normalize_review_contract(make_legacy_data("1.1"), "待校对");

    EXPECT_EQ(result.compatibility, ContractCompatibility::LegacyPendingReparse);
    EXPECT_EQ(result.data["contract"]["version"].asString(), "1.2");
    EXPECT_TRUE(result.data["ratings"]["component_ratings"].isArray());
}

TEST(ContractCompatibilityTest, MarksConfirmedLegacyAsLegacyReadOnly) {
    const auto result_10 = normalize_review_contract(make_legacy_data("1.0"), "已确认");
    const auto result_11 = normalize_review_contract(make_legacy_data("1.1"), "已确认");

    EXPECT_EQ(result_10.compatibility, ContractCompatibility::LegacyReadOnly);
    EXPECT_EQ(result_11.compatibility, ContractCompatibility::LegacyReadOnly);
    EXPECT_EQ(result_11.data["contract"]["version"].asString(), "1.2");
}

TEST(ContractCompatibilityTest, MarksCancelledLegacyAsLegacyReadOnly) {
    const auto result = normalize_review_contract(make_legacy_data("1.1"), "已取消");

    EXPECT_EQ(result.compatibility, ContractCompatibility::LegacyReadOnly);
}

TEST(ContractCompatibilityTest, LeavesNative12DataUnchanged) {
    auto data = make_legacy_data("1.2");
    data["defects"][0]["group_review_status"] = "已确认";
    data["defects"][0]["confirmed_missing_photo_numbers"] = Json::Value(Json::arrayValue);

    const auto result = normalize_review_contract(data, "待校对");

    EXPECT_EQ(result.compatibility, ContractCompatibility::Native12);
    EXPECT_EQ(result.data, data);
}

TEST(ContractCompatibilityTest, NamesCompatibilityValuesExactly) {
    EXPECT_EQ(bridge_report::review::contract_compatibility_name(ContractCompatibility::Native12), "native_1_2");
    EXPECT_EQ(
        bridge_report::review::contract_compatibility_name(ContractCompatibility::LegacyPendingReparse),
        "legacy_pending_reparse");
    EXPECT_EQ(
        bridge_report::review::contract_compatibility_name(ContractCompatibility::LegacyReadOnly),
        "legacy_read_only");
}

TEST(ContractCompatibilityTest, StoredContractRequiresReparseUnlessNative12) {
    EXPECT_TRUE(stored_contract_requires_reparse(make_legacy_data("1.0")));
    EXPECT_TRUE(stored_contract_requires_reparse(make_legacy_data("1.1")));
    EXPECT_TRUE(stored_contract_requires_reparse(Json::Value(Json::objectValue)));
    EXPECT_FALSE(stored_contract_requires_reparse(make_legacy_data("1.2")));
}
