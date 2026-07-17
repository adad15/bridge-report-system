#include <gtest/gtest.h>

#include "bridge_report/http/ImportRecordDeletionRoutes.hpp"

TEST(ImportRecordDeletionRoutesTest, ParsesCompleteConfirmationRequest) {
    Json::Value body;
    body["impact_token"] = "sha256:abc";
    body["confirmation_text"] = "永久删除 DRJL-000001";
    body["reason"] = "  重复上传  ";
    bridge_report::http::DeleteImportRecordRequest request;

    EXPECT_FALSE(bridge_report::http::parse_delete_import_record_request(body, request).has_value());
    EXPECT_EQ(request.reason, "重复上传");
    EXPECT_EQ(request.confirmation_text, "永久删除 DRJL-000001");
}

TEST(ImportRecordDeletionRoutesTest, RejectsMissingFieldsAndLongReason) {
    Json::Value body;
    body["confirmation_text"] = "永久删除 DRJL-000001";
    body["reason"] = "重复上传";
    bridge_report::http::DeleteImportRecordRequest request;
    EXPECT_EQ(bridge_report::http::parse_delete_import_record_request(body, request),
              "deletion_impact_token_required");
    body["impact_token"] = "sha256:abc";
    body["reason"] = std::string(1001, 'x');
    EXPECT_EQ(bridge_report::http::parse_delete_import_record_request(body, request),
              "deletion_reason_too_long");
}

TEST(ImportRecordDeletionRoutesTest, PreservesConfirmationWhitespaceForExactCheck) {
    Json::Value body;
    body["impact_token"] = "sha256:abc";
    body["confirmation_text"] = " 永久删除 DRJL-000001 ";
    body["reason"] = "重复上传";
    bridge_report::http::DeleteImportRecordRequest request;
    ASSERT_FALSE(bridge_report::http::parse_delete_import_record_request(body, request).has_value());
    EXPECT_EQ(request.confirmation_text, " 永久删除 DRJL-000001 ");
}
