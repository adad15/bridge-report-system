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

TEST(ContractCompatibilityTest, LeavesNative50DataUnchanged) {
    const auto data = make_contract("5.0");
    const auto result = normalize_review_contract(data, "待校对");
    EXPECT_EQ(result.compatibility, ContractCompatibility::Native50);
    EXPECT_EQ(result.data, data);
    EXPECT_EQ(
        bridge_report::review::contract_compatibility_name(result.compatibility),
        "native_5_0");
}

TEST(ContractCompatibilityTest, DoesNotNormalizeLegacyContracts) {
    const auto legacy = make_contract("1.2");
    const auto result = normalize_review_contract(legacy, "已确认");
    EXPECT_EQ(result.compatibility, ContractCompatibility::Native50);
    EXPECT_EQ(result.data, legacy);
    EXPECT_EQ(result.data["contract"]["version"].asString(), "1.2");
}

TEST(ContractCompatibilityTest, StoredContractRequiresReparseUnlessNative50) {
    EXPECT_TRUE(stored_contract_requires_reparse(make_contract("1.2")));
    EXPECT_TRUE(stored_contract_requires_reparse(make_contract("2.0")));
    EXPECT_TRUE(stored_contract_requires_reparse(Json::Value(Json::objectValue)));
    EXPECT_TRUE(stored_contract_requires_reparse(make_contract("3.0")));
    EXPECT_TRUE(stored_contract_requires_reparse(make_contract("4.0")));
    EXPECT_FALSE(stored_contract_requires_reparse(make_contract("5.0")));
}

TEST(ContractCompatibilityTest, DropsStaleComponentMatchWarnings) {
    // 5.0：构件绑定的权威来源是解析关系表。旧数据里残留的匹配警告要清掉，
    // 否则已经绑好的病害依旧顶着一条"未找到实际构件"。其他警告不能连坐。
    auto data = make_contract("5.0");
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "d1";
    defect["warnings"] = Json::Value(Json::arrayValue);
    for (const auto& code : {"defect_component_match_required",
                             "defect_component_match_ambiguous",
                             "defect_scale_invalid"}) {
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

// 这一条才是真正咬人的那个：normalize 不得给病害添任何键。
//
// 旧实现读 `bridge_component_id` / `component_match_method` /
// `component_match_candidate_ids` 来判定是否已解析，而 JsonCpp 非 const 的
// operator[] 读缺失键时会当场建一个 null 成员。于是 /review 响应里每条病害
// 都凭空多出两个 5.0 已删字段，前端契约守卫把整份数据拒掉，校对页只剩
// 一句"校对数据不符合 BridgeAnnualInspectionData 契约"。库里的数据一直是对的。
TEST(ContractCompatibilityTest, NeverAddsMembersToADefect) {
    auto data = make_contract("5.0");
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "d1";
    defect["warnings"] = Json::Value(Json::arrayValue);
    data["defects"].append(defect);

    const auto result = normalize_review_contract(data, "待校对");
    const auto& normalized = result.data["defects"][0];
    EXPECT_EQ(normalized.getMemberNames().size(), defect.getMemberNames().size());
    for (const auto* removed : {"bridge_component_id", "component_match_method",
                                "component_match_candidate_ids",
                                "standard_component_category_id", "rating_tree_node_id"}) {
        EXPECT_FALSE(normalized.isMember(removed)) << removed;
    }
    // 库里没警告时也不能凭空造一条出来。
    EXPECT_EQ(normalized["warnings"].size(), 0u);
}
