#include <gtest/gtest.h>

#include "bridge_report/http/InspectionYearDeletionRoutes.hpp"

TEST(InspectionYearDeletionRoutesTest, ParsesCompleteConfirmationRequest) {
    Json::Value body;
    body["impact_token"] = "sha256:abc";
    body["confirmation_text"] = "永久删除 2026";
    body["reason"] = "  误建年度  ";
    bridge_report::http::DeleteInspectionYearRequest request;
    EXPECT_FALSE(bridge_report::http::parse_delete_inspection_year_request(body, request).has_value());
    EXPECT_EQ(request.reason, "误建年度");
}

TEST(InspectionYearDeletionRoutesTest, RejectsMissingReasonAndToken) {
    Json::Value body;
    body["confirmation_text"] = "永久删除 2026";
    body["reason"] = "";
    bridge_report::http::DeleteInspectionYearRequest request;
    EXPECT_EQ(bridge_report::http::parse_delete_inspection_year_request(body, request),
              "deletion_impact_token_required");
    body["impact_token"] = "sha256:abc";
    EXPECT_EQ(bridge_report::http::parse_delete_inspection_year_request(body, request),
              "deletion_reason_required");
}

TEST(InspectionYearDeletionRoutesTest, PreservesConfirmationWhitespaceForExactBackendCheck) {
    Json::Value body;
    body["impact_token"] = "sha256:abc";
    body["confirmation_text"] = " 永久删除 2026 ";
    body["reason"] = "误建年度";
    bridge_report::http::DeleteInspectionYearRequest request;
    ASSERT_FALSE(bridge_report::http::parse_delete_inspection_year_request(body, request).has_value());
    EXPECT_EQ(request.confirmation_text, " 永久删除 2026 ");
}
