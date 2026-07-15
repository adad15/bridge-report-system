#include <gtest/gtest.h>

#include "bridge_report/http/WorkspaceRoutes.hpp"

using bridge_report::http::WorkspaceResource;
using bridge_report::http::workspace_not_found_body;

TEST(WorkspaceRoutesTest, BuildsStableBridgeNotFoundError) {
    const auto body = workspace_not_found_body(WorkspaceResource::Bridge);
    EXPECT_EQ(body["code"].asString(), "bridge_not_found");
    EXPECT_EQ(body["message"].asString(), "桥梁不存在。");
}

TEST(WorkspaceRoutesTest, BuildsStableInspectionYearNotFoundError) {
    const auto body = workspace_not_found_body(WorkspaceResource::InspectionYear);
    EXPECT_EQ(body["code"].asString(), "inspection_year_not_found");
    EXPECT_EQ(body["message"].asString(), "年度检测不存在。");
}
