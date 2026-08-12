#include <gtest/gtest.h>

#include <set>

#include "bridge_report/review/ComponentRangeSplitPlanner.hpp"

namespace {

using bridge_report::inventory::InventoryEntry;
using bridge_report::inventory::InventoryMapping;
using bridge_report::inventory::InventoryRevision;
using bridge_report::review::ComponentRangeSplitPlanStatus;
using bridge_report::review::ComponentRangeSplitTarget;
using bridge_report::review::analyze_component_range_splits;
using bridge_report::review::plan_component_range_splits;

InventoryEntry entry(std::string id, std::string number) {
    InventoryMapping mapping;
    mapping.id = "mapping-" + id;
    mapping.standard_component_category_id = "h21.component.beam.upper_bearing";
    mapping.structure_part = "superstructure";
    InventoryEntry value;
    value.id = "entry-" + id;
    value.bridge_component_id = std::move(id);
    value.component_number = std::move(number);
    value.site_name = "空心板";
    value.site_component_type = "空心板";
    value.mappings.push_back(std::move(mapping));
    return value;
}

InventoryRevision revision(std::vector<InventoryEntry> entries) {
    InventoryRevision value;
    value.id = "revision-1";
    value.status = "已确认";
    value.entries = std::move(entries);
    return value;
}

Json::Value defect(const std::string& id) {
    Json::Value value(Json::objectValue);
    value["candidate_id"] = id;
    value["component_name"] = "上部承重构件";
    value["component_number"] = "1-1#梁~1-3#梁";
    value["defect_description"] = "裂缝，宽0.2mm";
    value["defect_scale"] = 2;
    value["review_status"] = "已修改";
    value["group_review_status"] = "已确认";
    value["photo_numbers"].append("1.1-1");
    value["warnings"] = Json::Value(Json::arrayValue);
    return value;
}

Json::Value document(int defect_count = 1) {
    Json::Value value(Json::objectValue);
    value["defects"] = Json::Value(Json::arrayValue);
    for (int i = 0; i < defect_count; ++i) {
        value["defects"].append(defect("d" + std::to_string(i + 1)));
    }
    value["photos"] = Json::Value(Json::arrayValue);
    Json::Value photo(Json::objectValue);
    photo["candidate_id"] = "p1";
    photo["linked_defect_candidate_id"] = "d1";
    photo["extracted_file"]["archive_relative_path"] = "2026/photo.jpg";
    photo["warnings"] = Json::Value(Json::arrayValue);
    value["photos"].append(photo);
    value["warnings"] = Json::Value(Json::arrayValue);
    Json::Value warning(Json::objectValue);
    warning["code"] = "source-warning";
    warning["message"] = "待核对";
    warning["severity"] = "warning";
    warning["target_candidate_id"] = "d1";
    value["warnings"].append(warning);
    value["errors"] = Json::Value(Json::arrayValue);
    return value;
}

TEST(ComponentRangeSplitPlannerTest, ExpandsEveryReferencedDefectAndCopiesPhotos) {
    const auto plan = plan_component_range_splits(
        document(3),
        revision({entry("c1", "1-1#梁"), entry("c2", "1-2#梁"),
                  entry("c3", "1-3#梁")}),
        {ComponentRangeSplitTarget{"上部承重构件", "1-1#梁~1-3#梁"}});

    ASSERT_EQ(plan.status, ComponentRangeSplitPlanStatus::Ok);
    ASSERT_EQ(plan.items.size(), 1u);
    EXPECT_EQ(plan.items[0].source_defect_count, 3);
    EXPECT_EQ(plan.items[0].result_defect_count, 9);
    EXPECT_EQ(plan.items[0].result_photo_count, 3);
    EXPECT_EQ(plan.items[0].bound_count, 9);
    ASSERT_EQ(plan.result_json["defects"].size(), 9u);
    ASSERT_EQ(plan.result_json["photos"].size(), 3u);
    EXPECT_EQ(plan.result_json["photos"][0]["extracted_file"]["archive_relative_path"].asString(),
              "2026/photo.jpg");
    EXPECT_EQ(plan.result_json["warnings"].size(), 3u);

    std::set<std::string> ids;
    for (const auto& split : plan.result_json["defects"]) {
        ids.insert(split["candidate_id"].asString());
        EXPECT_EQ(split["defect_description"].asString(), "裂缝，宽0.2mm");
        EXPECT_EQ(split["defect_scale"].asInt(), 2);
        EXPECT_EQ(split["review_status"].asString(), "待确认");
        EXPECT_EQ(split["group_review_status"].asString(), "待确认");
        EXPECT_EQ(split["range_split_origin"]["source_component_number"].asString(),
                  "1-1#梁~1-3#梁");
        ASSERT_FALSE(split["warnings"].empty());
        EXPECT_EQ(split["warnings"][0]["code"].asString(),
                  "component_range_split_review_required");
    }
    EXPECT_EQ(ids.size(), 9u);
}

TEST(ComponentRangeSplitPlannerTest, LightweightAnalysisMatchesMaterializedSummary) {
    const auto current = document(3);
    const auto inventory = revision({entry("c1", "1-1#梁"), entry("c2", "1-2#梁"),
                                     entry("c3", "1-3#梁")});
    const auto targets = std::vector<ComponentRangeSplitTarget>{
        {"上部承重构件", "1-1#梁~1-3#梁"}};

    const auto analysis = analyze_component_range_splits(current, inventory, targets);
    const auto plan = plan_component_range_splits(current, inventory, targets);

    ASSERT_EQ(analysis.status, ComponentRangeSplitPlanStatus::Ok);
    ASSERT_EQ(analysis.items.size(), 1u);
    EXPECT_EQ(analysis.items[0].source_defect_count, 3);
    EXPECT_EQ(analysis.items[0].result_defect_count, 9);
    EXPECT_EQ(analysis.items[0].result_photo_count, 3);
    EXPECT_EQ(analysis.items[0].bound_count, 9);
    EXPECT_EQ(analysis.totals.result_defect_count, plan.totals.result_defect_count);
    EXPECT_EQ(analysis.totals.result_photo_count, plan.totals.result_photo_count);
    EXPECT_EQ(analysis.totals.bound_count, plan.totals.bound_count);
    ASSERT_EQ(analysis.work_items.size(), 1u);
    EXPECT_EQ(analysis.work_items[0].source_candidate_ids.size(), 3u);
    EXPECT_EQ(analysis.work_items[0].matches.size(), 3u);
}

TEST(ComponentRangeSplitPlannerTest, ReportsUniqueAmbiguousAndMissingMatches) {
    const auto plan = plan_component_range_splits(
        document(),
        revision({entry("c1", "1-1#梁"), entry("c1b", "1-1#梁"),
                  entry("c2", "1-2#梁")}),
        {ComponentRangeSplitTarget{"上部承重构件", "1-1#梁~1-3#梁"}});
    ASSERT_EQ(plan.status, ComponentRangeSplitPlanStatus::Ok);
    EXPECT_EQ(plan.totals.bound_count, 1);
    EXPECT_EQ(plan.totals.ambiguous_count, 1);
    EXPECT_EQ(plan.totals.unmatched_count, 1);
}

TEST(ComponentRangeSplitPlannerTest, RejectsBoundSourceAndResultLimit) {
    auto bound = document();
    bound["defects"][0]["bridge_component_id"] = "already-bound";
    auto rejected = plan_component_range_splits(
        bound, revision({}),
        {ComponentRangeSplitTarget{"上部承重构件", "1-1#梁~1-3#梁"}});
    EXPECT_EQ(rejected.status, ComponentRangeSplitPlanStatus::IneligibleTarget);

    const auto limited = plan_component_range_splits(
        document(3), revision({}),
        {ComponentRangeSplitTarget{"上部承重构件", "1-1#梁~1-3#梁"}}, 500, 8);
    EXPECT_EQ(limited.status, ComponentRangeSplitPlanStatus::ResultLimitExceeded);
}

}  // namespace
