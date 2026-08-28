#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/resolution/EffectiveDefectFacts.hpp"
#include "bridge_report/resolution/ResolutionHashes.hpp"

namespace {

using bridge_report::resolution::build_rating_match_hash_input;
using bridge_report::resolution::compute_rating_match_hashes;
using bridge_report::resolution::merge_effective_defect_facts;
using bridge_report::resolution::RatingMatchHashInput;

RatingMatchHashInput baseline() {
    RatingMatchHashInput input;
    input.source_candidate_id = "defect_0001";
    input.bridge_component_id = "component-1";
    input.technical_standard_package_id = "package-1";
    input.standard_bridge_type_id = "h21.bridge_type.beam";
    input.standard_component_category_id = "h21.component.beam.upper_bearing";
    input.rating_tree_version_id = "tree-1";
    input.source_defect_group_id = "jt-1";
    input.source_defect_group_number = "5.1.1";
    input.source_defect_indicator_id = "idx-spall";
    input.source_defect_indicator_number = "5.1.1-2";
    input.defect_type = "裂缝";
    input.defect_location = "底板";
    input.defect_description = "底板出现纵向裂缝";
    return input;
}

}  // namespace

TEST(ResolutionHashesTest, SameInputYieldsSameHashes) {
    const auto first = compute_rating_match_hashes(baseline());
    const auto second = compute_rating_match_hashes(baseline());

    EXPECT_EQ(first.applicability_hash, second.applicability_hash);
    EXPECT_EQ(first.match_input_hash, second.match_input_hash);
    EXPECT_NE(first.applicability_hash, first.match_input_hash);
}

// 这是拆成两个哈希的全部意义：改几个字不该把人工选的节点冲掉，
// 但换了构件/类别/桥型/评定树版本必须让它失效。
TEST(ResolutionHashesTest, TextChangesMoveOnlyTheMatchInputHash) {
    const auto base = compute_rating_match_hashes(baseline());

    for (auto mutate : {
             +[](RatingMatchHashInput& input) { input.defect_type = "剥落"; },
             +[](RatingMatchHashInput& input) { input.defect_location = "腹板"; },
             +[](RatingMatchHashInput& input) { input.defect_description = "改了描述"; },
             +[](RatingMatchHashInput& input) { input.source_defect_group_id = "jt-2"; },
             +[](RatingMatchHashInput& input) { input.source_defect_indicator_id = "idx-crack"; },
             +[](RatingMatchHashInput& input) { input.source_defect_group_number = "5.1.2"; },
             +[](RatingMatchHashInput& input) { input.source_defect_indicator_number = "5.1.2-1"; },
         }) {
        auto input = baseline();
        mutate(input);
        const auto changed = compute_rating_match_hashes(input);
        EXPECT_EQ(changed.applicability_hash, base.applicability_hash);
        EXPECT_NE(changed.match_input_hash, base.match_input_hash);
    }
}

TEST(ResolutionHashesTest, ApplicabilityChangesMoveBothHashes) {
    const auto base = compute_rating_match_hashes(baseline());

    for (auto mutate : {
             +[](RatingMatchHashInput& input) { input.bridge_component_id = "component-2"; },
             +[](RatingMatchHashInput& input) { input.rating_tree_version_id = "tree-2"; },
             +[](RatingMatchHashInput& input) { input.standard_bridge_type_id = "h21.bridge_type.arch"; },
             +[](RatingMatchHashInput& input) {
                 input.standard_component_category_id = "h21.component.arch.ring";
             },
             +[](RatingMatchHashInput& input) { input.technical_standard_package_id = "package-2"; },
         }) {
        auto input = baseline();
        mutate(input);
        const auto changed = compute_rating_match_hashes(input);
        EXPECT_NE(changed.applicability_hash, base.applicability_hash);
        EXPECT_NE(changed.match_input_hash, base.match_input_hash);
    }
}

// 标度变化不重新选择评分树节点，与现行行为一致（§19.3）；因此它不在任何一个哈希里。
TEST(ResolutionHashesTest, ScaleIsNotPartOfEitherHash) {
    Json::Value defect(Json::objectValue);
    defect["defect_type"] = "裂缝";
    defect["defect_location"] = "底板";
    defect["defect_description"] = "底板出现纵向裂缝";
    defect["defect_scale"] = 2;

    const auto with_two = compute_rating_match_hashes(build_rating_match_hash_input(
        defect, "defect_0001", "component-1", "package-1", "type-1", "category-1", "tree-1"));

    defect["defect_scale"] = 4;
    const auto with_four = compute_rating_match_hashes(build_rating_match_hash_input(
        defect, "defect_0001", "component-1", "package-1", "type-1", "category-1", "tree-1"));

    EXPECT_EQ(with_two.applicability_hash, with_four.applicability_hash);
    EXPECT_EQ(with_two.match_input_hash, with_four.match_input_hash);
}

// 哈希必须按**有效值**算。按来源值算的话，同一条来源病害展开出的多个实例哈希恒等，
// 覆盖再怎么改也触发不了失效。
TEST(ResolutionHashesTest, HashFollowsTheEffectiveFactNotTheSourceFact) {
    Json::Value source(Json::objectValue);
    source["defect_type"] = "裂缝";
    source["defect_location"] = "底板";
    source["defect_description"] = "底板出现纵向裂缝";

    Json::Value overrides(Json::objectValue);
    overrides["defect_type"] = "剥落";

    const auto from_source = compute_rating_match_hashes(build_rating_match_hash_input(
        source, "defect_0001", "component-1", "package-1", "type-1", "category-1", "tree-1"));
    const auto from_effective = compute_rating_match_hashes(build_rating_match_hash_input(
        merge_effective_defect_facts(source, overrides),
        "defect_0001", "component-1", "package-1", "type-1", "category-1", "tree-1"));

    EXPECT_EQ(from_source.applicability_hash, from_effective.applicability_hash);
    EXPECT_NE(from_source.match_input_hash, from_effective.match_input_hash);
}

// 同一条来源病害展开到两个构件：适用性不同，两个哈希都必须分开。
TEST(ResolutionHashesTest, TwoInstancesOfOneSourceDefectDoNotShareHashes) {
    auto left = baseline();
    auto right = baseline();
    right.bridge_component_id = "component-2";

    const auto left_hashes = compute_rating_match_hashes(left);
    const auto right_hashes = compute_rating_match_hashes(right);

    EXPECT_NE(left_hashes.applicability_hash, right_hashes.applicability_hash);
    EXPECT_NE(left_hashes.match_input_hash, right_hashes.match_input_hash);
}
