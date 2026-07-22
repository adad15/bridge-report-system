#include <gtest/gtest.h>

#include "bridge_report/http/ImportBindingRoutes.hpp"

namespace {

using bridge_report::db::BindingGroup;
using bridge_report::db::BindingOverview;
using bridge_report::db::BindingRow;

TEST(ImportBindingRoutesTest, SerializesOverviewForFrontend) {
    BindingOverview overview;
    overview.inventory_confirmed = true;

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
    group.rows.push_back(bound);

    BindingRow ambiguous;
    ambiguous.component_number = "1-2#梁";
    ambiguous.defect_count = 1;
    ambiguous.status = "ambiguous";
    ambiguous.candidate_component_ids = {"c2", "c3"};
    group.rows.push_back(ambiguous);

    overview.groups.push_back(group);

    const auto json = bridge_report::http::binding_overview_json(overview);
    EXPECT_TRUE(json["inventory_confirmed"].asBool());
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

    EXPECT_EQ(group_json["rows"][1]["status"].asString(), "ambiguous");
    EXPECT_TRUE(group_json["rows"][1]["bridge_component_id"].isNull());
    ASSERT_EQ(group_json["rows"][1]["candidate_component_ids"].size(), 2u);
    EXPECT_EQ(group_json["rows"][1]["candidate_component_ids"][0].asString(), "c2");
}

}  // namespace
