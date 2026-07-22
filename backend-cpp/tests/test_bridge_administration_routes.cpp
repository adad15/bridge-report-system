#include <gtest/gtest.h>

#include "bridge_report/http/BridgeAdministrationRoutes.hpp"

TEST(BridgeAdministrationRoutesTest, ParsesCreateAndDeleteRequests) {
    Json::Value create;
    create["bridge_name"] = "测试桥";
    bridge_report::db::CreateBridgeRequest create_request;
    EXPECT_FALSE(bridge_report::http::parse_create_bridge_request(create, create_request));
    EXPECT_EQ(create_request.status, "在用");

    Json::Value remove;
    remove["reason"] = "误建";
    remove["confirmation_text"] = "永久删除 QL-000001";
    Json::Value item; item["bridge_id"] = "11111111-1111-4111-8111-111111111111"; item["impact_token"] = "sha256:test";
    remove["items"].append(item);
    bridge_report::http::DeleteBridgesRequest delete_request;
    EXPECT_FALSE(bridge_report::http::parse_delete_bridges_request(remove, delete_request));
    EXPECT_EQ(delete_request.items.size(), 1u);
}

TEST(BridgeAdministrationRoutesTest, ParsesAndValidatesBridgeScale) {
    Json::Value body;
    body["bridge_name"] = "测试桥";
    body["bridge_scale"] = "大桥";
    bridge_report::db::CreateBridgeRequest request;
    EXPECT_FALSE(bridge_report::http::parse_create_bridge_request(body, request));
    ASSERT_TRUE(request.bridge_scale.has_value());
    EXPECT_EQ(*request.bridge_scale, "大桥");

    body["bridge_scale"] = "特大桥";  // 不在 大/中/小 集合
    bridge_report::db::CreateBridgeRequest invalid;
    EXPECT_EQ(bridge_report::http::parse_create_bridge_request(body, invalid), "invalid_bridge_scale");
}

TEST(BridgeAdministrationRoutesTest, RejectsDuplicateOrOversizedSelection) {
    Json::Value body;
    body["bridge_ids"].append("11111111-1111-4111-8111-111111111111");
    body["bridge_ids"].append("11111111-1111-4111-8111-111111111111");
    std::vector<std::string> ids;
    EXPECT_EQ(bridge_report::http::parse_bridge_selection(body, ids), "invalid_bridge_selection");
}
