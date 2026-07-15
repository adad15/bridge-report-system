#include <gtest/gtest.h>

#include "bridge_report/deletion/InspectionYearDeletionModels.hpp"

namespace {

bridge_report::deletion::InspectionYearDeletionPlan sample_plan() {
    bridge_report::deletion::InspectionYearDeletionPlan plan;
    plan.bridge_id = "bridge-1";
    plan.bridge_system_number = "QL-000001";
    plan.bridge_name = "绕阳河二号桥";
    plan.inspection_year = 2026;
    plan.version_numbers = {2, 1};
    plan.inspection_year_ids = {"year-2", "year-1"};
    plan.import_record_ids = {"import-2", "import-1"};
    plan.archived_file_ids_to_delete = {"file-2", "file-1"};
    plan.archived_file_relative_paths_to_delete = {"secret/path-2", "secret/path-1"};
    plan.fingerprint_items = {"year-2@b", "year-1@a"};
    plan.counts.inspection_versions = 2;
    plan.counts.import_records = 2;
    plan.counts.archived_files_to_delete = 2;
    return plan;
}

}  // namespace

TEST(InspectionYearDeletionModelsTest, ImpactTokenIsStableAcrossQueryOrder) {
    auto first = sample_plan();
    auto second = sample_plan();
    second.version_numbers = {1, 2};
    second.inspection_year_ids = {"year-1", "year-2"};
    second.import_record_ids = {"import-1", "import-2"};
    second.archived_file_ids_to_delete = {"file-1", "file-2"};
    second.fingerprint_items = {"year-1@a", "year-2@b"};

    EXPECT_EQ(first.impact_token(), second.impact_token());
    EXPECT_EQ(first.impact_token().rfind("sha256:", 0), 0u);
}

TEST(InspectionYearDeletionModelsTest, MembershipChangeInvalidatesImpactToken) {
    auto first = sample_plan();
    auto second = sample_plan();
    second.defect_observation_ids.push_back("observation-new");
    second.counts.defect_observations = 1;
    EXPECT_NE(first.impact_token(), second.impact_token());
}

TEST(InspectionYearDeletionModelsTest, PublicJsonNeverLeaksFileIdsOrPaths) {
    const auto json = sample_plan().to_public_json();
    const auto text = json.toStyledString();
    EXPECT_EQ(json["confirmation_text"].asString(), "永久删除 2026");
    EXPECT_EQ(json["version_numbers"].size(), 2u);
    EXPECT_EQ(json["counts"]["archived_files_to_delete"].asInt(), 2);
    EXPECT_EQ(text.find("secret/path"), std::string::npos);
    EXPECT_EQ(text.find("file-1"), std::string::npos);
}
