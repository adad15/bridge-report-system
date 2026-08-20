#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/assessment/AssessmentService.hpp"
#include "bridge_report/inventory/SideComponentPair.hpp"
#include "bridge_report/review/ComponentRangeSplitPlanner.hpp"
#include "support/h21_fixtures.hpp"

// 端到端：一条"两侧护栏"病害经侧别配对 → 多构件绑定 → 拆分落地 → 试算，
// 栏杆部件分必须是 56（百股大桥 BG-2024-02 的验收口径）。
//
// 各段都有自己的用例，这里要盯的是**接缝**：拆分产出的两条病害能不能被试算服务
// 认成两个独立的评分输入。中间任何一环把两条又合成一条、或漏掉一条，分数都会
// 回到 76，而单看各段的用例都是绿的。

namespace assessment = bridge_report::assessment;
namespace inventory = bridge_report::inventory;
namespace review = bridge_report::review;
namespace standards = bridge_report::standards;

namespace {

constexpr const char* kRailingCategory = "h21.component.deck.railing";
constexpr const char* kRailingIndicator = "h21.defect.10_4_1_2";
constexpr const char* kLeftId = "railing-left-instance";
constexpr const char* kRightId = "railing-right-instance";

inventory::InventoryEntry railing_entry(std::string id, std::string number) {
    inventory::InventoryMapping mapping;
    mapping.id = "mapping-" + id;
    mapping.standard_component_category_id = kRailingCategory;
    mapping.structure_part = "deck_system";
    mapping.is_active = true;
    inventory::InventoryEntry entry;
    entry.id = "entry-" + id;
    entry.bridge_component_id = std::move(id);
    entry.component_number = std::move(number);
    entry.site_name = "栏杆";
    entry.site_component_type = "栏杆";
    entry.mappings.push_back(std::move(mapping));
    return entry;
}

// 台账：栏杆是 {side}侧{name} 展开的左右两件。
inventory::InventoryRevision railing_inventory() {
    inventory::InventoryRevision revision;
    revision.id = "revision-1";
    revision.bridge_id = "bridge-1";
    revision.status = "已确认";
    revision.entries = {railing_entry(kLeftId, "左侧栏杆"),
                        railing_entry(kRightId, "右侧栏杆")};
    return revision;
}

// 试算上下文：把左右两件栏杆都放进参评构件，其余部件按完整梁桥补齐。
assessment::AssessmentContextSnapshot railing_context(
    const standards::StandardPackage& package) {
    assessment::AssessmentContextSnapshot context;
    context.standard_package_id = "11111111-1111-1111-1111-111111111111";
    context.standard_profile_id = "22222222-2222-2222-2222-222222222222";
    context.inventory_revision_id = "revision-1";
    context.inventory_confirmed = true;
    const auto input = bridge_report::tests::h21::complete_beam_input(package);
    context.bridge_type_id = input.bridge_type_id;
    for (const auto& component : input.components) {
        if (component.component_type_id == kRailingCategory) continue;
        context.components.push_back(
            {component.component_instance_id, component.component_type_id});
    }
    context.components.push_back({kLeftId, kRailingCategory});
    context.components.push_back({kRightId, kRailingCategory});
    context.rating_tree_version_id = "test-tree-version";
    context.rating_tree_content_checksum = "sha256:test-rating-tree";
    context.rating_tree = bridge_report::tests::h21::single_indicator_tree(
        context.bridge_type_id, kRailingCategory, kRailingIndicator, {1, 2, 3, 4});
    return context;
}

// 校对草稿：报告原样的一条"两侧护栏"，标度 3，尚未绑定构件。
Json::Value draft_with_two_sided_railing_defect() {
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "source_defect_0215";
    defect["component_name"] = "栏杆、护栏";
    defect["component_number"] = "两侧护栏";
    defect["defect_type"] = "基座破损露筋";
    defect["standard_defect_indicator_id"] = kRailingIndicator;
    defect["rating_tree_version_id"] = "test-tree-version";
    defect["rating_tree_node_id"] = "test-rating-tree-node";
    defect["defect_scale"] = 3;
    defect["review_status"] = "已修改";
    defect["warnings"] = Json::Value(Json::arrayValue);

    Json::Value draft(Json::objectValue);
    draft["defects"] = Json::Value(Json::arrayValue);
    draft["defects"].append(defect);
    draft["photos"] = Json::Value(Json::arrayValue);
    draft["ratings"]["overall"]["total_score"] = 1.0;
    return draft;
}

double railing_score(const standards::BridgeAssessmentResult& result) {
    for (const auto& part : result.structure_parts) {
        for (const auto& category : part.categories) {
            if (category.component_type_id == kRailingCategory) return category.score;
        }
    }
    throw std::runtime_error("railing category not found");
}

}  // namespace

TEST(TwoSidedBindingEndToEnd, SplittingTheRowAcrossBothRailingsScoresFiftySix) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    const auto context = railing_context(package);
    const auto revision = railing_inventory();

    // ① 配对判定认出左右两件。
    const auto pair = inventory::find_side_component_pair(revision, kRailingCategory);
    ASSERT_TRUE(pair.has_value());
    EXPECT_EQ(pair->left_bridge_component_id, kLeftId);
    EXPECT_EQ(pair->right_bridge_component_id, kRightId);

    // ② 按配对做多构件绑定，并落地成两条病害。
    const auto draft = draft_with_two_sided_railing_defect();
    const auto analysis = review::analyze_component_multi_bind(
        draft, revision, {"栏杆、护栏", "两侧护栏"},
        {pair->left_bridge_component_id, pair->right_bridge_component_id});
    ASSERT_EQ(analysis.status, review::ComponentRangeSplitPlanStatus::Ok)
        << analysis.error_message;
    const auto plan = review::materialize_component_range_splits(draft, analysis);
    ASSERT_EQ(plan.status, review::ComponentRangeSplitPlanStatus::Ok);
    ASSERT_EQ(plan.result_json["defects"].size(), 2u);

    // ③ 拆分后的草稿交给试算：两条必须各自成为一个评分输入。
    const auto preview = assessment::calculate_assessment_preview(
        evaluator, package, context, plan.result_json, 1);
    ASSERT_TRUE(preview.result.has_value());
    ASSERT_TRUE(preview.issues.empty())
        << "拆分产物不该带出任何入库前阻断：" << preview.issues.front().message;
    EXPECT_DOUBLE_EQ(railing_score(*preview.result), 56.0);
}

// 对照组：同一条病害只绑左侧（修复前的做法），右侧留在满分，部件分 76。
// 两条并排放着，56 与 76 的来源就一目了然。
TEST(TwoSidedBindingEndToEnd, BindingOnlyTheLeftRailingScoresSeventySix) {
    const auto package = bridge_report::tests::h21::load_package();
    const standards::H21Evaluator evaluator(package);
    const auto context = railing_context(package);

    auto draft = draft_with_two_sided_railing_defect();
    draft["defects"][0]["bridge_component_id"] = kLeftId;
    draft["defects"][0]["standard_component_category_id"] = kRailingCategory;

    const auto preview = assessment::calculate_assessment_preview(
        evaluator, package, context, draft, 1);
    ASSERT_TRUE(preview.result.has_value());
    EXPECT_DOUBLE_EQ(railing_score(*preview.result), 76.0);
}
