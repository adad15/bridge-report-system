#include <gtest/gtest.h>
#include <json/value.h>

#include "bridge_report/review/ReviewStatistics.hpp"

using bridge_report::review::build_review_statistics;

namespace {

Json::Value make_candidate(const std::string& review_status, bool with_warning = false) {
    Json::Value candidate;
    candidate["review_status"] = review_status;
    Json::Value warnings(Json::arrayValue);
    if (with_warning) {
        warnings.append("疑似重复病害");
    }
    candidate["warnings"] = warnings;
    return candidate;
}

Json::Value make_rating_item(const std::string& review_status) {
    Json::Value item;
    item["review_status"] = review_status;
    return item;
}

}  // namespace

TEST(ReviewStatisticsTest, EmptyObjectYieldsAllZero) {
    Json::Value parsed_result(Json::objectValue);

    const auto stats = build_review_statistics(parsed_result);

    EXPECT_EQ(stats.defect_count, 0);
    EXPECT_EQ(stats.photo_count, 0);
    EXPECT_EQ(stats.rating_item_count, 0);
    EXPECT_EQ(stats.pending_count, 0);
    EXPECT_EQ(stats.confirmed_count, 0);
    EXPECT_EQ(stats.modified_count, 0);
    EXPECT_EQ(stats.ignored_count, 0);
    EXPECT_EQ(stats.object_warning_count, 0);
}

TEST(ReviewStatisticsTest, EmptyArraysYieldZeroCounts) {
    Json::Value parsed_result(Json::objectValue);
    parsed_result["defects"] = Json::Value(Json::arrayValue);
    parsed_result["photos"] = Json::Value(Json::arrayValue);

    const auto stats = build_review_statistics(parsed_result);

    EXPECT_EQ(stats.defect_count, 0);
    EXPECT_EQ(stats.photo_count, 0);
    EXPECT_EQ(stats.rating_item_count, 0);
    EXPECT_EQ(stats.object_warning_count, 0);
}

TEST(ReviewStatisticsTest, CountsDefectsAndPhotos) {
    Json::Value parsed_result(Json::objectValue);
    Json::Value defects(Json::arrayValue);
    defects.append(make_candidate("待确认"));
    defects.append(make_candidate("已确认"));
    parsed_result["defects"] = defects;

    Json::Value photos(Json::arrayValue);
    photos.append(make_candidate("已修改"));
    parsed_result["photos"] = photos;

    const auto stats = build_review_statistics(parsed_result);

    EXPECT_EQ(stats.defect_count, 2);
    EXPECT_EQ(stats.photo_count, 1);
}

TEST(ReviewStatisticsTest, RatingItemCountIncludesOverallPlusPartsWhenPresent) {
    Json::Value parsed_result(Json::objectValue);
    Json::Value ratings(Json::objectValue);
    ratings["overall"] = make_rating_item("待确认");

    Json::Value structure_parts(Json::arrayValue);
    structure_parts.append(make_rating_item("待确认"));
    structure_parts.append(make_rating_item("待确认"));
    ratings["structure_parts"] = structure_parts;

    Json::Value evaluation_parts(Json::arrayValue);
    evaluation_parts.append(make_rating_item("待确认"));
    evaluation_parts.append(make_rating_item("待确认"));
    evaluation_parts.append(make_rating_item("待确认"));
    ratings["evaluation_parts"] = evaluation_parts;

    parsed_result["ratings"] = ratings;

    const auto stats = build_review_statistics(parsed_result);

    // overall (1) + structure_parts (2) + evaluation_parts (3) = 6
    EXPECT_EQ(stats.rating_item_count, 6);
}

TEST(ReviewStatisticsTest, RatingItemCountExcludesOverallWhenAbsent) {
    Json::Value parsed_result(Json::objectValue);
    Json::Value ratings(Json::objectValue);

    Json::Value structure_parts(Json::arrayValue);
    structure_parts.append(make_rating_item("待确认"));
    ratings["structure_parts"] = structure_parts;

    parsed_result["ratings"] = ratings;

    const auto stats = build_review_statistics(parsed_result);

    // no overall -> +0; structure_parts (1) -> total 1
    EXPECT_EQ(stats.rating_item_count, 1);
}

TEST(ReviewStatisticsTest, ReviewStatusAggregatesAcrossDefectsPhotosAndRatings) {
    Json::Value parsed_result(Json::objectValue);

    Json::Value defects(Json::arrayValue);
    defects.append(make_candidate("待确认"));
    defects.append(make_candidate("已确认"));
    parsed_result["defects"] = defects;

    Json::Value photos(Json::arrayValue);
    photos.append(make_candidate("已修改"));
    photos.append(make_candidate("已忽略"));
    parsed_result["photos"] = photos;

    Json::Value ratings(Json::objectValue);
    ratings["overall"] = make_rating_item("待确认");

    Json::Value structure_parts(Json::arrayValue);
    structure_parts.append(make_rating_item("已确认"));
    ratings["structure_parts"] = structure_parts;

    Json::Value evaluation_parts(Json::arrayValue);
    evaluation_parts.append(make_rating_item("已修改"));
    evaluation_parts.append(make_rating_item("已忽略"));
    ratings["evaluation_parts"] = evaluation_parts;

    parsed_result["ratings"] = ratings;

    const auto stats = build_review_statistics(parsed_result);

    // pending: defect(1) + rating overall(1) = 2
    EXPECT_EQ(stats.pending_count, 2);
    // confirmed: defect(1) + rating structure_parts(1) = 2
    EXPECT_EQ(stats.confirmed_count, 2);
    // modified: photo(1) + rating evaluation_parts(1) = 2
    EXPECT_EQ(stats.modified_count, 2);
    // ignored: photo(1) + rating evaluation_parts(1) = 2
    EXPECT_EQ(stats.ignored_count, 2);
}

TEST(ReviewStatisticsTest, ObjectWarningCountCountsNonEmptyWarningsInDefectsAndPhotosOnly) {
    Json::Value parsed_result(Json::objectValue);

    Json::Value defects(Json::arrayValue);
    defects.append(make_candidate("待确认", /*with_warning=*/true));
    defects.append(make_candidate("已确认", /*with_warning=*/false));
    parsed_result["defects"] = defects;

    Json::Value photos(Json::arrayValue);
    photos.append(make_candidate("待确认", /*with_warning=*/true));
    parsed_result["photos"] = photos;

    // Ratings items have no warnings[] field per contract; ensure they don't affect this count.
    Json::Value ratings(Json::objectValue);
    ratings["overall"] = make_rating_item("待确认");
    parsed_result["ratings"] = ratings;

    const auto stats = build_review_statistics(parsed_result);

    EXPECT_EQ(stats.object_warning_count, 2);
}

TEST(ReviewStatisticsTest, ToJsonOutputsAllFields) {
    bridge_report::review::ReviewStatistics stats{};
    stats.defect_count = 1;
    stats.photo_count = 2;
    stats.rating_item_count = 3;
    stats.pending_count = 4;
    stats.confirmed_count = 5;
    stats.modified_count = 6;
    stats.ignored_count = 7;
    stats.object_warning_count = 8;

    const auto json = stats.to_json();

    EXPECT_EQ(json["defect_count"].asInt(), 1);
    EXPECT_EQ(json["photo_count"].asInt(), 2);
    EXPECT_EQ(json["rating_item_count"].asInt(), 3);
    EXPECT_EQ(json["pending_count"].asInt(), 4);
    EXPECT_EQ(json["confirmed_count"].asInt(), 5);
    EXPECT_EQ(json["modified_count"].asInt(), 6);
    EXPECT_EQ(json["ignored_count"].asInt(), 7);
    EXPECT_EQ(json["object_warning_count"].asInt(), 8);
}
