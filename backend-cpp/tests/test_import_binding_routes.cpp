#include <gtest/gtest.h>

#include "bridge_report/http/ImportBindingRoutes.hpp"

namespace {

using bridge_report::db::BindingGroup;
using bridge_report::db::BindingOverview;
using bridge_report::db::BindingComponentSummary;
using bridge_report::db::BindingRow;

TEST(ImportBindingRoutesTest, SerializesOverviewForFrontend) {
    BindingOverview overview;
    overview.inventory_confirmed = true;
    overview.rating_tree = bridge_report::db::BindingRatingTree{
        "tree-1", "单位桥梁有效评定树", "1.0.2", "1.0.3", "1.0.0"};

    BindingGroup group;
    group.part_name = "上部承重构件";
    group.total = 2;
    group.bound = 1;
    group.unmatched = 1;

    BindingRow bound;
    bound.component_number = "1-1#梁";
    bound.defect_count = 3;
    bound.status = "bound";
    bound.bridge_component_id = "component-1";
    bound.bound_component = BindingComponentSummary{
        "entry-1", "component-1", "1-1#梁", "空心板", "第一跨空心板"};
    group.rows.push_back(bound);

    BindingRow ambiguous;
    ambiguous.component_number = "1-2#梁";
    ambiguous.defect_count = 1;
    ambiguous.status = "ambiguous";
    // 内部 id 有两个，可显示的只有一个：另一个已停用或没有生效映射。
    ambiguous.candidate_component_ids = {"c2", "c3"};
    ambiguous.candidate_components = {
        BindingComponentSummary{"entry-2", "c2", "1-2#梁", "空心板", "第二跨空心板"}};
    group.rows.push_back(ambiguous);

    overview.groups.push_back(group);

    const auto json = bridge_report::http::binding_overview_json(overview);
    EXPECT_TRUE(json["inventory_confirmed"].asBool());
    EXPECT_EQ(
        json["rating_tree"]["tree_name"].asString(),
        "单位桥梁有效评定树");
    EXPECT_EQ(
        json["rating_tree"]["h21_package_version"].asString(),
        "1.0.3");
    ASSERT_EQ(json["groups"].size(), 1u);
    const auto& group_json = json["groups"][0];
    EXPECT_EQ(group_json["part_name"].asString(), "上部承重构件");
    EXPECT_EQ(group_json["total"].asInt(), 2);
    EXPECT_EQ(group_json["bound"].asInt(), 1);
    EXPECT_EQ(group_json["unmatched"].asInt(), 1);
    ASSERT_EQ(group_json["rows"].size(), 2u);

    EXPECT_EQ(group_json["rows"][0]["component_number"].asString(), "1-1#梁");
    EXPECT_EQ(group_json["rows"][0]["defect_count"].asInt(), 3);
    EXPECT_EQ(group_json["rows"][0]["status"].asString(), "bound");
    EXPECT_EQ(group_json["rows"][0]["bridge_component_id"].asString(), "component-1");

    // 下拉的 key 用 entry_id、value 用 bridge_component_id，两者都得在。
    const auto& bound_json = group_json["rows"][0]["bound_component"];
    EXPECT_EQ(bound_json["entry_id"].asString(), "entry-1");
    EXPECT_EQ(bound_json["component_number"].asString(), "1-1#梁");
    EXPECT_EQ(bound_json["site_name"].asString(), "第一跨空心板");

    EXPECT_EQ(group_json["rows"][1]["status"].asString(), "ambiguous");
    EXPECT_TRUE(group_json["rows"][1]["bridge_component_id"].isNull());
    EXPECT_TRUE(group_json["rows"][1]["bound_component"].isNull());
    // 内部候选 id 不再进 JSON：前端拿裸 id 只能靠拉整份台账去换成编号。
    EXPECT_FALSE(group_json["rows"][1].isMember("candidate_component_ids"));
    ASSERT_EQ(group_json["rows"][1]["candidate_components"].size(), 1u);
    EXPECT_EQ(group_json["rows"][1]["candidate_components"][0]["bridge_component_id"].asString(),
              "c2");
}

}  // namespace
