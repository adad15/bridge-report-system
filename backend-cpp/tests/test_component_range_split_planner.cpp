#include <gtest/gtest.h>

#include <set>

#include "bridge_report/review/ComponentRangeSplitPlanner.hpp"

namespace {

using bridge_report::inventory::InventoryEntry;
using bridge_report::inventory::InventoryMapping;
using bridge_report::inventory::InventoryRevision;
using bridge_report::review::ComponentRangeSplitPlanStatus;
using bridge_report::review::ComponentRangeSplitTarget;
using bridge_report::review::analyze_component_multi_bind;
using bridge_report::review::analyze_component_range_splits;
using bridge_report::review::materialize_component_range_splits;
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

TEST(ComponentRangeSplitPlannerTest, ExpandsEveryReferencedDefectAndKeepsPhotosOnTheFirstSplit) {
    const auto plan = plan_component_range_splits(
        document(3),
        revision({entry("c1", "1-1#梁"), entry("c2", "1-2#梁"),
                  entry("c3", "1-3#梁")}),
        {ComponentRangeSplitTarget{"上部承重构件", "1-1#梁~1-3#梁"}});

    ASSERT_EQ(plan.status, ComponentRangeSplitPlanStatus::Ok);
    ASSERT_EQ(plan.items.size(), 1u);
    EXPECT_EQ(plan.items[0].source_defect_count, 3);
    EXPECT_EQ(plan.items[0].result_defect_count, 9);
    // 照片不再随构件数翻倍：源文档就一张，拆完还是一张。
    EXPECT_EQ(plan.items[0].result_photo_count, 1);
    EXPECT_EQ(plan.items[0].bound_count, 9);
    ASSERT_EQ(plan.result_json["defects"].size(), 9u);
    ASSERT_EQ(plan.result_json["photos"].size(), 1u);
    EXPECT_EQ(plan.result_json["photos"][0]["extracted_file"]["archive_relative_path"].asString(),
              "2026/photo.jpg");
    // 那一张挂在 d1 的第一条拆分结果上，其余八条一张都不带。
    EXPECT_EQ(plan.result_json["photos"][0]["linked_defect_candidate_id"].asString(),
              "d1__range_1");
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
    EXPECT_EQ(analysis.items[0].result_photo_count, 1);
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


// ---------- "两侧"多构件绑定 ----------

namespace two_sided {

InventoryEntry railing(std::string id, std::string number) {
    InventoryMapping mapping;
    mapping.id = "mapping-" + id;
    mapping.standard_component_category_id = "h21.component.deck.railing";
    mapping.structure_part = "deck_system";
    InventoryEntry value;
    value.id = "entry-" + id;
    value.bridge_component_id = std::move(id);
    value.component_number = std::move(number);
    value.site_name = "栏杆";
    value.site_component_type = "栏杆";
    value.mappings.push_back(std::move(mapping));
    return value;
}

InventoryRevision inventory() {
    return revision({railing("c-left", "左侧栏杆"), railing("c-right", "右侧栏杆"),
                     entry("c-girder", "1-1#梁")});
}

// 百股大桥那条：报告编号"两侧护栏"，标度 3，总面积 80 ㎡，带一张照片。
Json::Value document() {
    Json::Value defect_value(Json::objectValue);
    defect_value["candidate_id"] = "r0";
    defect_value["component_name"] = "栏杆、护栏";
    defect_value["component_number"] = "两侧护栏";
    defect_value["defect_description"] = "基座破损露筋";
    defect_value["defect_scale"] = 3;
    defect_value["review_status"] = "已修改";
    defect_value["group_review_status"] = "已确认";
    defect_value["warnings"] = Json::Value(Json::arrayValue);
    Json::Value measurement(Json::objectValue);
    measurement["value"] = 80.0;
    measurement["value_type"] = "总面积";
    defect_value["measurements"] = Json::Value(Json::arrayValue);
    defect_value["measurements"].append(measurement);

    Json::Value photo(Json::objectValue);
    photo["candidate_id"] = "p0";
    photo["linked_defect_candidate_id"] = "r0";
    photo["warnings"] = Json::Value(Json::arrayValue);

    Json::Value value(Json::objectValue);
    value["defects"] = Json::Value(Json::arrayValue);
    value["defects"].append(defect_value);
    value["photos"] = Json::Value(Json::arrayValue);
    value["photos"].append(photo);
    return value;
}

ComponentRangeSplitTarget target() { return {"栏杆、护栏", "两侧护栏"}; }

TEST(ComponentMultiBindTest, BuildsOneMatchPerChosenComponent) {
    const auto analysis = analyze_component_multi_bind(
        document(), inventory(), target(), {"c-left", "c-right"});
    ASSERT_EQ(analysis.status, ComponentRangeSplitPlanStatus::Ok) << analysis.error_message;
    ASSERT_EQ(analysis.work_items.size(), 1u);
    const auto& work = analysis.work_items.front();
    ASSERT_EQ(work.matches.size(), 2u);
    // 编号取台账真实编号，不是报告里的"两侧护栏"——"护栏 vs 栏杆"用词不一致
    // 正是在这里消解的。
    EXPECT_EQ(work.matches[0].component_number, "左侧栏杆");
    EXPECT_EQ(work.matches[0].bridge_component_id, "c-left");
    EXPECT_EQ(work.matches[0].standard_component_category_id, "h21.component.deck.railing");
    EXPECT_EQ(work.matches[0].resolved_structure_part, "桥面系");
    // 构件是人选的，来源必须记成 manual，与既有单条绑定一致。
    EXPECT_EQ(work.matches[0].match_method, "manual");
    // 顺序即所选顺序，不得重排。
    EXPECT_EQ(work.matches[1].component_number, "右侧栏杆");
    EXPECT_EQ(work.matches[1].bridge_component_id, "c-right");
    EXPECT_EQ(analysis.totals.result_defect_count, 2);
    EXPECT_EQ(analysis.totals.bound_count, 2);
    EXPECT_EQ(analysis.totals.unmatched_count, 0);
}

TEST(ComponentMultiBindTest, MaterializesTwoBoundDefects) {
    const auto source = document();
    const auto analysis = analyze_component_multi_bind(
        source, inventory(), target(), {"c-left", "c-right"});
    ASSERT_EQ(analysis.status, ComponentRangeSplitPlanStatus::Ok);
    const auto plan = materialize_component_range_splits(source, analysis);
    ASSERT_EQ(plan.status, ComponentRangeSplitPlanStatus::Ok);

    const auto& defects = plan.result_json["defects"];
    ASSERT_EQ(defects.size(), 2u);
    for (Json::ArrayIndex i = 0; i < 2; ++i) {
        EXPECT_EQ(defects[i]["review_status"].asString(), "待确认");
        EXPECT_EQ(defects[i]["component_match_method"].asString(), "manual");
        // 标度与病害量原样复制：评分只取标度，量值留给人核（沿用范围拆分的既有约定）。
        EXPECT_EQ(defects[i]["defect_scale"].asInt(), 3);
        ASSERT_EQ(defects[i]["measurements"].size(), 1u);
        EXPECT_DOUBLE_EQ(defects[i]["measurements"][0]["value"].asDouble(), 80.0);
        // 溯源必须指回那条"两侧护栏"，否则出了问题查不到它从哪来。
        EXPECT_EQ(defects[i]["range_split_origin"]["source_candidate_id"].asString(), "r0");
        EXPECT_EQ(defects[i]["range_split_origin"]["source_component_number"].asString(),
                  "两侧护栏");
        EXPECT_EQ(defects[i]["range_split_origin"]["split_count"].asInt(), 2);
    }
    EXPECT_EQ(defects[0]["component_number"].asString(), "左侧栏杆");
    EXPECT_EQ(defects[0]["bridge_component_id"].asString(), "c-left");
    EXPECT_EQ(defects[1]["component_number"].asString(), "右侧栏杆");
    EXPECT_EQ(defects[1]["bridge_component_id"].asString(), "c-right");
}

// "两侧护栏"拆成左右两条时，原文那几张图只有人能判断该配给哪一侧。整份复制会让
// 同一个编号同时出现在两条上，编号交叉引用作废；照片全留第一条，人工往另一侧挪。
TEST(ComponentMultiBindTest, KeepsPhotosOnTheFirstSideOnly) {
    const auto source = document();
    const auto analysis = analyze_component_multi_bind(
        source, inventory(), target(), {"c-left", "c-right"});
    ASSERT_EQ(analysis.status, ComponentRangeSplitPlanStatus::Ok);
    EXPECT_EQ(analysis.totals.result_photo_count, 1);
    const auto plan = materialize_component_range_splits(source, analysis);

    const auto& photos = plan.result_json["photos"];
    ASSERT_EQ(photos.size(), 1u);
    const auto& defects = plan.result_json["defects"];
    ASSERT_EQ(defects.size(), 2u);
    // 照片指向第一条（左侧），不能还指着已经不存在的源候选。
    EXPECT_EQ(photos[0]["linked_defect_candidate_id"].asString(),
              defects[0]["candidate_id"].asString());
    EXPECT_NE(photos[0]["linked_defect_candidate_id"].asString(), "r0");
}

// 不带照片的那一侧连 Word 引用也不留：留着只会变成一排"待核对"缺图卡，还会挡住入库。
TEST(ComponentMultiBindTest, LeavesNoDanglingPhotoReferencesOnTheOtherSide) {
    auto source = document();
    Json::Value reference(Json::objectValue);
    reference["photo_number"] = "2.3-14";
    reference["resolution"] = "matched";
    reference["photo_candidate_id"] = "p0";
    reference["resolved_defect_candidate_id"] = "r0";
    reference["review_note"] = Json::Value();
    source["defects"][0]["photo_references"] = Json::Value(Json::arrayValue);
    source["defects"][0]["photo_references"].append(reference);

    const auto analysis = analyze_component_multi_bind(
        source, inventory(), target(), {"c-left", "c-right"});
    ASSERT_EQ(analysis.status, ComponentRangeSplitPlanStatus::Ok);
    const auto plan = materialize_component_range_splits(source, analysis);

    const auto& defects = plan.result_json["defects"];
    ASSERT_EQ(defects.size(), 2u);
    ASSERT_EQ(defects[0]["photo_references"].size(), 1u);
    EXPECT_EQ(defects[0]["photo_references"][0]["photo_number"].asString(), "2.3-14");
    // 引用重写到第一条自己的那份照片候选上。
    EXPECT_EQ(defects[0]["photo_references"][0]["photo_candidate_id"].asString(),
              plan.result_json["photos"][0]["candidate_id"].asString());
    EXPECT_EQ(defects[0]["photo_references"][0]["resolved_defect_candidate_id"].asString(),
              defects[0]["candidate_id"].asString());
    EXPECT_EQ(defects[1]["photo_references"].size(), 0u);
}

TEST(ComponentMultiBindTest, RejectsAlreadyResolvedRows) {
    auto source = document();
    source["defects"][0]["bridge_component_id"] = "c-left";
    const auto analysis = analyze_component_multi_bind(
        source, inventory(), target(), {"c-left", "c-right"});
    EXPECT_EQ(analysis.status, ComponentRangeSplitPlanStatus::IneligibleTarget);
}

TEST(ComponentMultiBindTest, RejectsAComponentOutsideTheInventory) {
    const auto analysis = analyze_component_multi_bind(
        document(), inventory(), target(), {"c-left", "c-nonexistent"});
    EXPECT_EQ(analysis.status, ComponentRangeSplitPlanStatus::InvalidTarget);
}

// 类别不符：梁不能绑到"栏杆、护栏"上。与单条绑定同一条规则。
TEST(ComponentMultiBindTest, RejectsAComponentOfAnotherPart) {
    const auto analysis = analyze_component_multi_bind(
        document(), inventory(), target(), {"c-left", "c-girder"});
    EXPECT_EQ(analysis.status, ComponentRangeSplitPlanStatus::InvalidTarget);
}

TEST(ComponentMultiBindTest, RequiresAtLeastTwoComponents) {
    EXPECT_EQ(analyze_component_multi_bind(document(), inventory(), target(), {}).status,
              ComponentRangeSplitPlanStatus::InvalidTarget);
    EXPECT_EQ(
        analyze_component_multi_bind(document(), inventory(), target(), {"c-left"}).status,
        ComponentRangeSplitPlanStatus::InvalidTarget);
}

// 同一个构件选两次会让它吃到两份同样的扣分，必须拒绝。
TEST(ComponentMultiBindTest, RejectsDuplicateComponents) {
    const auto analysis = analyze_component_multi_bind(
        document(), inventory(), target(), {"c-left", "c-left"});
    EXPECT_EQ(analysis.status, ComponentRangeSplitPlanStatus::InvalidTarget);
}

TEST(ComponentMultiBindTest, RejectsWhenTheTargetRowIsGone) {
    const auto analysis = analyze_component_multi_bind(
        document(), inventory(), {"栏杆、护栏", "不存在的编号"}, {"c-left", "c-right"});
    EXPECT_EQ(analysis.status, ComponentRangeSplitPlanStatus::InvalidTarget);
    EXPECT_EQ(analysis.error_code, "component_multi_bind_target_not_found");
}

}  // namespace two_sided

}  // namespace
