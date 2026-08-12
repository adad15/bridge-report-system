#include <gtest/gtest.h>
#include <json/value.h>

#include "bridge_report/review/ReviewStatistics.hpp"

namespace {

Json::Value candidate(const char* status, bool warning = false) {
    Json::Value value(Json::objectValue);
    value["review_status"] = status;
    value["warnings"] = Json::Value(Json::arrayValue);
    if (warning) value["warnings"].append(Json::Value(Json::objectValue));
    return value;
}

Json::Value photo_candidate(bool warning = false) {
    Json::Value value(Json::objectValue);
    value["warnings"] = Json::Value(Json::arrayValue);
    if (warning) value["warnings"].append(Json::Value(Json::objectValue));
    return value;
}

}  // namespace

TEST(ReviewStatisticsTest, CountsDefectsAndPhotosOnly) {
    Json::Value data(Json::objectValue);
    data["defects"].append(candidate("待确认", true));
    data["defects"].append(candidate("已修改"));
    data["photos"].append(photo_candidate());
    data["photos"].append(photo_candidate());
    const auto stats = bridge_report::review::build_review_statistics(data);
    EXPECT_EQ(stats.defect_count, 2);
    EXPECT_EQ(stats.photo_count, 2);
    EXPECT_EQ(stats.rating_item_count, 0);
    EXPECT_EQ(stats.pending_count, 1);
    EXPECT_EQ(stats.confirmed_count, 0);
    EXPECT_EQ(stats.modified_count, 1);
    EXPECT_EQ(stats.ignored_count, 0);
    EXPECT_EQ(stats.object_warning_count, 1);
}

TEST(ReviewStatisticsTest, IgnoresUnexpectedRatingObjects) {
    Json::Value data(Json::objectValue);
    data["defects"] = Json::Value(Json::arrayValue);
    data["photos"] = Json::Value(Json::arrayValue);
    data["ratings"]["overall"] = candidate("待确认");
    const auto stats = bridge_report::review::build_review_statistics(data);
    EXPECT_EQ(stats.rating_item_count, 0);
    EXPECT_EQ(stats.pending_count, 0);
}

TEST(ReviewStatisticsTest, EmptyOrMalformedCandidateCollectionsYieldZero) {
    for (auto data : {Json::Value(Json::objectValue), Json::Value(Json::nullValue)}) {
        const auto stats = bridge_report::review::build_review_statistics(data);
        EXPECT_EQ(stats.defect_count, 0);
        EXPECT_EQ(stats.photo_count, 0);
        EXPECT_EQ(stats.rating_item_count, 0);
        EXPECT_EQ(stats.pending_count, 0);
        EXPECT_EQ(stats.object_warning_count, 0);
    }
}

TEST(ReviewStatisticsTest, CountsOnlyNonEmptyDefectAndPhotoWarningArrays) {
    Json::Value data(Json::objectValue);
    data["defects"].append(candidate("已确认", true));
    data["defects"].append(candidate("已确认", false));
    data["photos"].append(photo_candidate(true));
    data["warnings"].append(Json::Value(Json::objectValue));
    const auto stats = bridge_report::review::build_review_statistics(data);
    EXPECT_EQ(stats.object_warning_count, 2);
    EXPECT_EQ(stats.confirmed_count, 2);
    EXPECT_EQ(stats.modified_count, 0);
}

TEST(ReviewStatisticsTest, ToJsonKeepsStableZeroRatingCountAndAllStatusCounts) {
    Json::Value data(Json::objectValue);
    data["defects"].append(candidate("待确认"));
    data["photos"].append(photo_candidate(true));
    const auto json = bridge_report::review::build_review_statistics(data).to_json();
    EXPECT_EQ(json["defect_count"].asInt(), 1);
    EXPECT_EQ(json["photo_count"].asInt(), 1);
    EXPECT_EQ(json["rating_item_count"].asInt(), 0);
    EXPECT_EQ(json["pending_count"].asInt(), 1);
    EXPECT_EQ(json["ignored_count"].asInt(), 0);
    EXPECT_EQ(json["object_warning_count"].asInt(), 1);
}
