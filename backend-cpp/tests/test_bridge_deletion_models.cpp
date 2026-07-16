#include <gtest/gtest.h>

#include "bridge_report/deletion/BridgeDeletionModels.hpp"

TEST(BridgeDeletionModelsTest, FingerprintAndConfirmationAreOrderIndependent) {
    bridge_report::deletion::BridgeDeletionPlan first;
    first.bridge_id = "b1";
    first.bridge_system_number = "QL-000002";
    first.fingerprint_items = {"year:y2", "year:y1"};
    auto second = first;
    second.fingerprint_items = {"year:y1", "year:y2"};
    EXPECT_EQ(first.impact_token(), second.impact_token());
    EXPECT_EQ(
        bridge_report::deletion::bridge_deletion_confirmation_text({"QL-000002", "QL-000001"}),
        "永久删除 QL-000001、QL-000002"
    );
}

TEST(BridgeDeletionModelsTest, PublicJsonDoesNotExposeFileIdentifiersOrPaths) {
    bridge_report::deletion::BridgeDeletionPlan plan;
    plan.bridge_id = "b1";
    plan.bridge_system_number = "QL-000001";
    plan.bridge_name = "测试桥";
    plan.status = "在用";
    plan.archived_file_ids_to_delete = {"secret-id"};
    plan.archived_file_relative_paths_to_delete = {"secret/path.docx"};
    const auto serialized = plan.to_public_json().toStyledString();
    EXPECT_EQ(serialized.find("secret-id"), std::string::npos);
    EXPECT_EQ(serialized.find("secret/path.docx"), std::string::npos);
}
