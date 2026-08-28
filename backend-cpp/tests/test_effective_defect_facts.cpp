#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/resolution/EffectiveDefectFacts.hpp"

namespace {

using bridge_report::resolution::fact_overrides_are_valid;
using bridge_report::resolution::merge_effective_defect_facts;
using bridge_report::resolution::overridden_fact_fields;

Json::Value source_defect() {
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "defect_0001";
    defect["component_name"] = "上部承重构件";
    defect["component_number"] = "1~3#梁";
    defect["defect_type"] = "裂缝";
    defect["defect_location"] = "底板";
    defect["defect_description"] = "底板出现纵向裂缝";
    defect["defect_scale"] = 2;
    defect["quantity_text"] = "3处";
    defect["measurement_text"] = "L=1.2m";
    defect["remark"] = Json::Value();
    defect["measurements"] = Json::Value(Json::arrayValue);
    return defect;
}

Json::Value overrides_with(const std::string& key, const Json::Value& value) {
    Json::Value overrides(Json::objectValue);
    overrides[key] = value;
    return overrides;
}

}  // namespace

TEST(EffectiveDefectFactsTest, WithoutOverridesTheSourceFactsPassThrough) {
    const auto effective =
        merge_effective_defect_facts(source_defect(), Json::Value(Json::objectValue));

    EXPECT_EQ(effective, source_defect());
    EXPECT_TRUE(overridden_fact_fields(Json::Value(Json::objectValue)).empty());
}

TEST(EffectiveDefectFactsTest, OverrideWinsOverTheSourceValue) {
    const auto overrides = overrides_with("defect_type", Json::Value("剥落"));

    const auto effective = merge_effective_defect_facts(source_defect(), overrides);

    EXPECT_EQ(effective["defect_type"].asString(), "剥落");
    // 没被覆盖的字段必须原样留着，否则合并就变成了"用覆盖替换整条病害"。
    EXPECT_EQ(effective["defect_location"].asString(), "底板");
    EXPECT_EQ(effective["candidate_id"].asString(), "defect_0001");
    EXPECT_EQ(overridden_fact_fields(overrides), std::vector<std::string>{"defect_type"});
}

// 删键才是"撤销覆盖"。接口层若把清除实现成写 null，必填事实就会带着 null 进预检。
TEST(EffectiveDefectFactsTest, ClearingAnOverrideRestoresTheSourceValue) {
    Json::Value overrides(Json::objectValue);
    overrides["defect_scale"] = 4;
    EXPECT_EQ(merge_effective_defect_facts(source_defect(), overrides)["defect_scale"].asInt(), 4);

    overrides.removeMember("defect_scale");
    EXPECT_EQ(merge_effective_defect_facts(source_defect(), overrides)["defect_scale"].asInt(), 2);
}

TEST(EffectiveDefectFactsTest, NullableFieldsMayBeOverriddenToNull) {
    const auto overrides = overrides_with("remark", Json::Value());
    std::string reason;

    EXPECT_TRUE(fact_overrides_are_valid(overrides, reason)) << reason;
    EXPECT_TRUE(merge_effective_defect_facts(source_defect(), overrides)["remark"].isNull());
}

TEST(EffectiveDefectFactsTest, RequiredFactsCannotBeOverriddenToNull) {
    for (const auto* field : {"defect_type", "defect_location", "defect_description"}) {
        std::string reason;
        EXPECT_FALSE(
            fact_overrides_are_valid(overrides_with(field, Json::Value()), reason))
            << field;
        EXPECT_NE(reason.find(field), std::string::npos) << reason;
    }
}

// 覆盖 JSON 不是第二个 parsed_result：解析字段一旦能从这里混进有效事实，
// §4.4 划的边界就形同虚设。
TEST(EffectiveDefectFactsTest, ResolutionFieldsAreNotOverridable) {
    for (const auto* field : {
             "bridge_component_id", "rating_tree_node_id", "candidate_id",
             "photo_references", "review_status", "source_ref"}) {
        std::string reason;
        EXPECT_FALSE(
            fact_overrides_are_valid(overrides_with(field, Json::Value("x")), reason))
            << field;
    }

    // 即使有人绕过校验把它塞进 JSON，合并也不得让它生效。
    const auto smuggled =
        overrides_with("bridge_component_id", Json::Value("component-999"));
    const auto effective = merge_effective_defect_facts(source_defect(), smuggled);
    EXPECT_FALSE(effective.isMember("bridge_component_id"));
}

TEST(EffectiveDefectFactsTest, ScaleMustStayAPositiveInteger) {
    std::string reason;
    EXPECT_TRUE(fact_overrides_are_valid(overrides_with("defect_scale", 3), reason));
    EXPECT_TRUE(
        fact_overrides_are_valid(overrides_with("defect_scale", Json::Value()), reason));
    EXPECT_FALSE(fact_overrides_are_valid(overrides_with("defect_scale", 0), reason));
    EXPECT_FALSE(fact_overrides_are_valid(overrides_with("defect_scale", -1), reason));
    EXPECT_FALSE(
        fact_overrides_are_valid(overrides_with("defect_scale", Json::Value("2")), reason));
}

TEST(EffectiveDefectFactsTest, MeasurementsMustBeAnArray) {
    std::string reason;
    EXPECT_TRUE(fact_overrides_are_valid(
        overrides_with("measurements", Json::Value(Json::arrayValue)), reason));
    EXPECT_FALSE(fact_overrides_are_valid(
        overrides_with("measurements", Json::Value("L=1.2m")), reason));
}
