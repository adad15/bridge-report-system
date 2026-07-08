#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/review/ConfirmPlan.hpp"

namespace {

using bridge_report::review::build_confirm_plan;
using bridge_report::review::ComponentPlan;
using bridge_report::review::ConfirmPlan;
using bridge_report::review::DefectPlan;
using bridge_report::review::MeasurementPlan;
using bridge_report::review::PhotoPlan;
using bridge_report::review::RatingPlan;

Json::Value read_contract_fixture(const std::string& file_name) {
    const auto path = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) / "samples" / "contracts" / file_name;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open fixture: " + path.string());
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors)) {
        throw std::runtime_error("Unable to parse fixture: " + path.string() + ": " + errors);
    }

    return root;
}

Json::Value valid_data() {
    return read_contract_fixture("bridge_annual_inspection_data.valid.json");
}

// 与 test_preflight_report.cpp 中同名函数逻辑一致：把样例三层候选（defects/photos/ratings）
// 的 review_status 全部改为“已确认”，作为“标准入库样本”基底。consolidation into a shared
// test header is planned for Task 8；目前两处测试各自保留一份。
void confirm_all_candidates(Json::Value& data) {
    for (auto& defect : data["defects"]) {
        defect["review_status"] = "已确认";
    }
    for (auto& photo : data["photos"]) {
        photo["review_status"] = "已确认";
        photo["match_status"] = "已确认";
    }
    data["ratings"]["overall"]["review_status"] = "已确认";
    for (auto& part : data["ratings"]["structure_parts"]) {
        part["review_status"] = "已确认";
    }
    for (auto& part : data["ratings"]["evaluation_parts"]) {
        part["review_status"] = "已确认";
    }
}

const ComponentPlan* find_component(const ConfirmPlan& plan, const std::string& normalized_key) {
    for (const auto& component : plan.components) {
        if (component.normalized_component_key == normalized_key) {
            return &component;
        }
    }
    return nullptr;
}

const DefectPlan* find_defect(const ConfirmPlan& plan, const std::string& candidate_id) {
    for (const auto& defect : plan.defects) {
        if (defect.candidate_id == candidate_id) {
            return &defect;
        }
    }
    return nullptr;
}

const PhotoPlan* find_photo(const ConfirmPlan& plan, const std::string& candidate_id) {
    for (const auto& photo : plan.photos) {
        if (photo.candidate_id == candidate_id) {
            return &photo;
        }
    }
    return nullptr;
}

const RatingPlan* find_rating(const ConfirmPlan& plan, const std::string& rating_level, const std::string& rating_item_name) {
    for (const auto& rating : plan.ratings) {
        if (rating.rating_level == rating_level && rating.rating_item_name == rating_item_name) {
            return &rating;
        }
    }
    return nullptr;
}

const MeasurementPlan* find_measurement(const DefectPlan& defect, const std::string& measurement_type) {
    for (const auto& measurement : defect.measurements) {
        if (measurement.measurement_type == measurement_type) {
            return &measurement;
        }
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// Rule 1: only 已确认/已修改 defects enter the plan
// ---------------------------------------------------------------------------

TEST(ConfirmPlanTest, DefectConfirmedEntersPlanWithComponentDetails) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto plan = build_confirm_plan(data);

    const auto* defect = find_defect(plan, "defect_0001");
    ASSERT_NE(defect, nullptr);
    EXPECT_EQ(defect->review_status, "已确认");
    EXPECT_EQ(defect->component_key, "上部结构|上部承重构件|主梁");

    ASSERT_EQ(plan.components.size(), 1u);
    EXPECT_EQ(plan.components[0].normalized_component_key, "上部结构|上部承重构件|主梁");
    EXPECT_EQ(plan.components[0].component_type, "上部承重构件");
    EXPECT_EQ(plan.components[0].business_component_code, "主梁");
    ASSERT_TRUE(plan.components[0].alias_text.has_value());
    EXPECT_EQ(*plan.components[0].alias_text, "上部承重构件");
}

TEST(ConfirmPlanTest, DefectIgnoredDoesNotEnterPlan) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["review_status"] = "已忽略";

    const auto plan = build_confirm_plan(data);

    EXPECT_EQ(find_defect(plan, "defect_0001"), nullptr);
    EXPECT_TRUE(plan.defects.empty());
    EXPECT_TRUE(plan.components.empty());
}

TEST(ConfirmPlanTest, DefectPendingDoesNotEnterPlan) {
    auto data = valid_data();
    // defects[0].review_status stays 待确认 (fixture default) — preflight would normally
    // block this; ConfirmPlan should skip it defensively regardless.

    const auto plan = build_confirm_plan(data);

    EXPECT_TRUE(plan.defects.empty());
}

TEST(ConfirmPlanTest, DefectModifiedStatusEntersPlan) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["review_status"] = "已修改";

    const auto plan = build_confirm_plan(data);

    const auto* defect = find_defect(plan, "defect_0001");
    ASSERT_NE(defect, nullptr);
    EXPECT_EQ(defect->review_status, "已修改");
}

// ---------------------------------------------------------------------------
// Rule 2: component dedup + alias fallback + normalized key
// ---------------------------------------------------------------------------

TEST(ConfirmPlanTest, SameComponentTwoDefectsProduceOneComponentPlan) {
    auto data = valid_data();
    confirm_all_candidates(data);

    Json::Value second = data["defects"][0];
    second["candidate_id"] = "defect_0002";
    second["defect_location"] = "第三跨右幅梁底";
    data["defects"].append(second);

    const auto plan = build_confirm_plan(data);

    ASSERT_EQ(plan.defects.size(), 2u);
    ASSERT_EQ(plan.components.size(), 1u);
    EXPECT_EQ(plan.defects[0].component_key, plan.defects[1].component_key);
}

TEST(ConfirmPlanTest, DifferentComponentTwoDefectsProduceTwoComponentPlans) {
    auto data = valid_data();
    confirm_all_candidates(data);

    Json::Value second = data["defects"][0];
    second["candidate_id"] = "defect_0002";
    second["component_name"] = "横梁";
    second["component_alias"] = Json::Value(Json::nullValue);
    data["defects"].append(second);

    const auto plan = build_confirm_plan(data);

    ASSERT_EQ(plan.defects.size(), 2u);
    ASSERT_EQ(plan.components.size(), 2u);
}

TEST(ConfirmPlanTest, ComponentAliasEmptyFallsBackToComponentNameAndAliasTextUnset) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["component_alias"] = Json::Value(Json::nullValue);

    const auto plan = build_confirm_plan(data);

    ASSERT_EQ(plan.components.size(), 1u);
    EXPECT_EQ(plan.components[0].component_type, "主梁");
    EXPECT_EQ(plan.components[0].business_component_code, "主梁");
    EXPECT_FALSE(plan.components[0].alias_text.has_value());
    EXPECT_EQ(plan.components[0].normalized_component_key, "上部结构|主梁|主梁");
}

TEST(ConfirmPlanTest, NormalizedComponentKeyTrimsSurroundingWhitespace) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["component_name"] = " 主梁 ";
    data["defects"][0]["component_alias"] = Json::Value(Json::nullValue);

    Json::Value second = data["defects"][0];
    second["candidate_id"] = "defect_0002";
    second["component_name"] = "主梁";
    data["defects"].append(second);

    const auto plan = build_confirm_plan(data);

    ASSERT_EQ(plan.defects.size(), 2u);
    ASSERT_EQ(plan.components.size(), 1u);
    EXPECT_EQ(plan.components[0].normalized_component_key, "上部结构|主梁|主梁");
}

TEST(ConfirmPlanTest, NormalizedComponentKeyCollapsesInternalWhitespace) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["component_name"] = "1-1#   板";
    data["defects"][0]["component_alias"] = Json::Value(Json::nullValue);

    const auto plan = build_confirm_plan(data);

    ASSERT_EQ(plan.components.size(), 1u);
    EXPECT_EQ(plan.components[0].normalized_component_key, "上部结构|1-1# 板|1-1# 板");
    ASSERT_NE(find_component(plan, "上部结构|1-1# 板|1-1# 板"), nullptr);
}

// ---------------------------------------------------------------------------
// Rule 3: defect field-by-field mapping
// ---------------------------------------------------------------------------

TEST(ConfirmPlanTest, DefectFieldMappingFromSourceRefAndReview) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["review_note"] = "复核后确认为轻微裂缝";

    const auto plan = build_confirm_plan(data);

    const auto* defect = find_defect(plan, "defect_0001");
    ASSERT_NE(defect, nullptr);
    ASSERT_TRUE(defect->part_name.has_value());
    EXPECT_EQ(*defect->part_name, "上部承重构件");
    EXPECT_EQ(defect->defect_location, "第二跨左幅梁底");
    EXPECT_EQ(defect->defect_type, "裂缝");
    EXPECT_EQ(defect->defect_description_raw, "梁底发现纵向裂缝，需现场复核。");
    ASSERT_TRUE(defect->scale.has_value());
    EXPECT_EQ(*defect->scale, "warning");
    ASSERT_TRUE(defect->raw_row_text.has_value());
    EXPECT_EQ(*defect->raw_row_text, "第二跨左幅梁底主梁裂缝，L=0.8m，W=0.12mm。");
    ASSERT_TRUE(defect->source_table_title.has_value());
    EXPECT_EQ(*defect->source_table_title, "病害记录表");
    ASSERT_TRUE(defect->source_table_index.has_value());
    EXPECT_EQ(*defect->source_table_index, 1);
    ASSERT_TRUE(defect->source_row_number.has_value());
    EXPECT_EQ(*defect->source_row_number, 1);
    EXPECT_DOUBLE_EQ(defect->extraction_confidence, 0.92);
    ASSERT_TRUE(defect->review_note.has_value());
    EXPECT_EQ(*defect->review_note, "复核后确认为轻微裂缝");
}

TEST(ConfirmPlanTest, DefectReviewNoteAbsentWhenNull) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto plan = build_confirm_plan(data);

    const auto* defect = find_defect(plan, "defect_0001");
    ASSERT_NE(defect, nullptr);
    EXPECT_FALSE(defect->review_note.has_value());
}

// ---------------------------------------------------------------------------
// Rule 4: measurements mapping
// ---------------------------------------------------------------------------

TEST(ConfirmPlanTest, MeasurementsMapFromMeasurementsArray) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["quantity_text"] = Json::Value(Json::nullValue);  // isolate from quantity auto-append

    const auto plan = build_confirm_plan(data);

    const auto* defect = find_defect(plan, "defect_0001");
    ASSERT_NE(defect, nullptr);
    ASSERT_EQ(defect->measurements.size(), 2u);

    const auto* length = find_measurement(*defect, "长度");
    ASSERT_NE(length, nullptr);
    ASSERT_TRUE(length->numeric_value.has_value());
    EXPECT_DOUBLE_EQ(*length->numeric_value, 0.8);
    ASSERT_TRUE(length->unit.has_value());
    EXPECT_EQ(*length->unit, "m");
    EXPECT_EQ(length->raw_text, "L=0.8m");
    EXPECT_TRUE(length->is_auto_parsed);

    const auto* width = find_measurement(*defect, "宽度");
    ASSERT_NE(width, nullptr);
    ASSERT_TRUE(width->numeric_value.has_value());
    EXPECT_DOUBLE_EQ(*width->numeric_value, 0.12);
    ASSERT_TRUE(width->unit.has_value());
    EXPECT_EQ(*width->unit, "mm");
    EXPECT_EQ(width->raw_text, "W=0.12mm");
    EXPECT_TRUE(width->is_auto_parsed);
}

TEST(ConfirmPlanTest, MeasurementsEmptyWithMeasurementTextProducesUnrecognizedRow) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["measurements"] = Json::Value(Json::arrayValue);
    data["defects"][0]["quantity_text"] = Json::Value(Json::nullValue);

    const auto plan = build_confirm_plan(data);

    const auto* defect = find_defect(plan, "defect_0001");
    ASSERT_NE(defect, nullptr);
    ASSERT_EQ(defect->measurements.size(), 1u);
    const auto& measurement = defect->measurements[0];
    EXPECT_EQ(measurement.measurement_type, "未识别尺寸");
    EXPECT_FALSE(measurement.numeric_value.has_value());
    EXPECT_FALSE(measurement.unit.has_value());
    EXPECT_EQ(measurement.raw_text, "L=0.8m，W=0.12mm");
    EXPECT_FALSE(measurement.is_auto_parsed);
}

TEST(ConfirmPlanTest, QuantityTextWithLeadingIntegerAppendsQuantityRow) {
    auto data = valid_data();
    confirm_all_candidates(data);
    // measurements already has 长度/宽度 (no 数量); quantity_text is "1处" from the fixture.

    const auto plan = build_confirm_plan(data);

    const auto* defect = find_defect(plan, "defect_0001");
    ASSERT_NE(defect, nullptr);
    ASSERT_EQ(defect->measurements.size(), 3u);
    const auto* quantity = find_measurement(*defect, "数量");
    ASSERT_NE(quantity, nullptr);
    ASSERT_TRUE(quantity->numeric_value.has_value());
    EXPECT_DOUBLE_EQ(*quantity->numeric_value, 1.0);
    EXPECT_FALSE(quantity->unit.has_value());
    EXPECT_EQ(quantity->raw_text, "1处");
    EXPECT_FALSE(quantity->is_auto_parsed);
}

TEST(ConfirmPlanTest, QuantityTextWithoutLeadingIntegerYieldsNulloptValue) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["quantity_text"] = "约三处";

    const auto plan = build_confirm_plan(data);

    const auto* defect = find_defect(plan, "defect_0001");
    ASSERT_NE(defect, nullptr);
    const auto* quantity = find_measurement(*defect, "数量");
    ASSERT_NE(quantity, nullptr);
    EXPECT_FALSE(quantity->numeric_value.has_value());
    EXPECT_EQ(quantity->raw_text, "约三处");
}

TEST(ConfirmPlanTest, QuantityTextSkippedWhenMeasurementsAlreadyHasQuantityItem) {
    auto data = valid_data();
    confirm_all_candidates(data);
    Json::Value quantity_row;
    quantity_row["dimension_type"] = "数量";
    quantity_row["value"] = 2;
    quantity_row["unit"] = "处";
    quantity_row["source_text"] = "2处";
    data["defects"][0]["measurements"].append(quantity_row);
    // quantity_text remains "1处" from the fixture; must not append a second 数量 row.

    const auto plan = build_confirm_plan(data);

    const auto* defect = find_defect(plan, "defect_0001");
    ASSERT_NE(defect, nullptr);
    int quantity_count = 0;
    for (const auto& measurement : defect->measurements) {
        if (measurement.measurement_type == "数量") {
            ++quantity_count;
        }
    }
    EXPECT_EQ(quantity_count, 1);
    const auto* quantity = find_measurement(*defect, "数量");
    ASSERT_NE(quantity, nullptr);
    EXPECT_EQ(quantity->raw_text, "2处");
    EXPECT_TRUE(quantity->is_auto_parsed);
}

// ---------------------------------------------------------------------------
// Rule 5: photos
// ---------------------------------------------------------------------------

TEST(ConfirmPlanTest, PhotoConfirmedLinkedToPlannedDefectEntersPlan) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto plan = build_confirm_plan(data);

    const auto* photo = find_photo(plan, "photo_0001");
    ASSERT_NE(photo, nullptr);
    EXPECT_EQ(photo->defect_candidate_id, "defect_0001");
    EXPECT_EQ(photo->photo_number, "2.1-1");
    ASSERT_TRUE(photo->photo_title.has_value());
    EXPECT_EQ(*photo->photo_title, "主梁梁底裂缝");
}

TEST(ConfirmPlanTest, PhotoLinkedToIgnoredDefectDoesNotEnterPlan) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["defects"][0]["review_status"] = "已忽略";

    const auto plan = build_confirm_plan(data);

    EXPECT_EQ(find_photo(plan, "photo_0001"), nullptr);
}

TEST(ConfirmPlanTest, PhotoUnlinkedDoesNotEnterPlan) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["photos"][0]["linked_defect_candidate_id"] = Json::Value(Json::nullValue);

    const auto plan = build_confirm_plan(data);

    EXPECT_TRUE(plan.photos.empty());
}

TEST(ConfirmPlanTest, PhotoIgnoredDoesNotEnterPlan) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["photos"][0]["review_status"] = "已忽略";

    const auto plan = build_confirm_plan(data);

    EXPECT_TRUE(plan.photos.empty());
}

TEST(ConfirmPlanTest, PhotoPendingDoesNotEnterPlan) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["photos"][0]["review_status"] = "待确认";

    const auto plan = build_confirm_plan(data);

    EXPECT_TRUE(plan.photos.empty());
}

TEST(ConfirmPlanTest, PhotoModifiedStatusEntersPlan) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["photos"][0]["review_status"] = "已修改";

    const auto plan = build_confirm_plan(data);

    ASSERT_NE(find_photo(plan, "photo_0001"), nullptr);
}

// ---------------------------------------------------------------------------
// Rule 6/7: ratings three-layer mapping + overall top-level sync
// ---------------------------------------------------------------------------

TEST(ConfirmPlanTest, RatingOverallMapsToFullBridgeLevel) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto plan = build_confirm_plan(data);

    const auto* overall = find_rating(plan, "全桥", "全桥");
    ASSERT_NE(overall, nullptr);
    EXPECT_EQ(overall->structure_part, "全桥");
    ASSERT_TRUE(overall->score.has_value());
    EXPECT_DOUBLE_EQ(*overall->score, 85.61);
    ASSERT_TRUE(overall->grade.has_value());
    EXPECT_EQ(*overall->grade, "2类");
    EXPECT_FALSE(overall->weight.has_value());
    EXPECT_FALSE(overall->remarks.has_value());
    EXPECT_EQ(overall->review_status, "已确认");
}

TEST(ConfirmPlanTest, RatingStructurePartMapsWithWeightAndGrade) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto plan = build_confirm_plan(data);

    const auto* part = find_rating(plan, "结构分部", "下部结构");
    ASSERT_NE(part, nullptr);
    EXPECT_EQ(part->structure_part, "下部结构");
    ASSERT_TRUE(part->score.has_value());
    EXPECT_DOUBLE_EQ(*part->score, 86.61);
    ASSERT_TRUE(part->grade.has_value());
    EXPECT_EQ(*part->grade, "2");
    ASSERT_TRUE(part->weight.has_value());
    EXPECT_DOUBLE_EQ(*part->weight, 0.4);
    EXPECT_FALSE(part->remarks.has_value());
}

TEST(ConfirmPlanTest, RatingEvaluationPartMapsScoreRowsToRemarks) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto plan = build_confirm_plan(data);

    const auto* part = find_rating(plan, "部件", "上部承重构件");
    ASSERT_NE(part, nullptr);
    EXPECT_EQ(part->structure_part, "上部结构");
    ASSERT_TRUE(part->score.has_value());
    EXPECT_DOUBLE_EQ(*part->score, 86.62);
    EXPECT_FALSE(part->grade.has_value());
    EXPECT_FALSE(part->weight.has_value());
    ASSERT_TRUE(part->remarks.has_value());

    Json::CharReaderBuilder reader_builder;
    Json::Value parsed;
    std::string parse_errors;
    std::istringstream stream(*part->remarks);
    ASSERT_TRUE(Json::parseFromStream(reader_builder, stream, &parsed, &parse_errors)) << parse_errors;
    ASSERT_TRUE(parsed.isArray());
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0]["component_count"].asInt(), 3);
    EXPECT_DOUBLE_EQ(parsed[0]["component_score"].asDouble(), 86.62);
}

TEST(ConfirmPlanTest, RatingEvaluationPartEmptyScoreRowsYieldsNulloptRemarks) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["evaluation_parts"][0]["score_rows"] = Json::Value(Json::arrayValue);

    const auto plan = build_confirm_plan(data);

    const auto* part = find_rating(plan, "部件", "上部承重构件");
    ASSERT_NE(part, nullptr);
    EXPECT_FALSE(part->remarks.has_value());
}

TEST(ConfirmPlanTest, RatingStructurePartPendingSkipped) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["structure_parts"][1]["review_status"] = "待确认";

    const auto plan = build_confirm_plan(data);

    EXPECT_EQ(find_rating(plan, "结构分部", "下部结构"), nullptr);
}

TEST(ConfirmPlanTest, RatingEvaluationPartIgnoredSkipped) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["evaluation_parts"][2]["review_status"] = "已忽略";

    const auto plan = build_confirm_plan(data);

    EXPECT_EQ(find_rating(plan, "部件", "支座"), nullptr);
}

TEST(ConfirmPlanTest, OverallScoreAndGradeSyncedToTopLevel) {
    auto data = valid_data();
    confirm_all_candidates(data);

    const auto plan = build_confirm_plan(data);

    ASSERT_TRUE(plan.overall_score.has_value());
    EXPECT_DOUBLE_EQ(*plan.overall_score, 85.61);
    EXPECT_EQ(plan.overall_grade, "2类");
}

TEST(ConfirmPlanTest, OverallScoreAndGradeEmptyWhenOverallNotSettled) {
    auto data = valid_data();
    confirm_all_candidates(data);
    data["ratings"]["overall"]["review_status"] = "待确认";

    const auto plan = build_confirm_plan(data);

    EXPECT_FALSE(plan.overall_score.has_value());
    EXPECT_EQ(plan.overall_grade, "");
    EXPECT_EQ(find_rating(plan, "全桥", "全桥"), nullptr);
}
