#include <gtest/gtest.h>

#include "bridge_report/deletion/ImportRecordDeletionModels.hpp"

using namespace bridge_report::deletion;

namespace {

ImportRecordDeletionPlan plan() {
    ImportRecordDeletionPlan value;
    value.import_record_id = "11111111-1111-1111-1111-111111111111";
    value.import_system_number = "DRJL-000001";
    value.import_name = "测试导入";
    value.import_status = "待校对";
    value.source_type = "软件导出Word";
    value.updated_at = "2026-07-17 09:00:00+08";
    value.bridge_id = "22222222-2222-2222-2222-222222222222";
    value.bridge_system_number = "QL-000001";
    value.bridge_name = "测试桥";
    value.inspection_year_id = "33333333-3333-3333-3333-333333333333";
    value.inspection_year = 2026;
    value.inspection_version = 1;
    return value;
}

}  // namespace

TEST(ImportRecordDeletionModelsTest, AllowsOnlyNonFormalStatuses) {
    auto value = plan();
    EXPECT_TRUE(value.can_delete());
    value.import_status = "已确认";
    EXPECT_FALSE(value.can_delete());
    EXPECT_EQ(value.block_code(), "import_record_not_deletable");
}

TEST(ImportRecordDeletionModelsTest, LockAndFormalFactsBlockDeletion) {
    auto value = plan();
    value.active_edit_locks.push_back({value.import_record_id, "zhang", "张工", "a", "b"});
    EXPECT_EQ(value.block_code(), "import_record_edit_locked");
    value.active_edit_locks.clear();
    value.counts.formal_fact_references = 1;
    EXPECT_EQ(value.block_code(), "import_record_has_formal_facts");
}

TEST(ImportRecordDeletionModelsTest, TokenIsOrderIndependentAndPublicJsonIsRedacted) {
    auto left = plan();
    left.fingerprint_items = {"b", "a"};
    left.archived_file_relative_paths_to_delete = {"secret/path.jpg"};
    auto right = left;
    right.fingerprint_items = {"a", "b"};
    EXPECT_EQ(left.impact_token(), right.impact_token());
    const auto json = left.to_public_json().toStyledString();
    EXPECT_EQ(json.find("secret/path.jpg"), std::string::npos);
    EXPECT_EQ(left.confirmation_text(), "永久删除 DRJL-000001");
}
