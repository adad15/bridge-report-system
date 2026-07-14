#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/db/ComponentArchiveRepository.hpp"

using bridge_report::db::assemble_defect_archive;

namespace {

Json::Value make_observation(const std::string& id, int year, const Json::Value& thread_id) {
    Json::Value observation;
    observation["id"] = id;
    observation["inspection_year"] = year;
    observation["defect_thread_id"] = thread_id;
    observation["defect_type"] = "蜂窝、麻面";
    observation["defect_location"] = "左侧端部";
    return observation;
}

Json::Value make_thread(const std::string& id, const std::string& name) {
    Json::Value thread;
    thread["id"] = id;
    thread["thread_name"] = name;
    thread["defect_type"] = "蜂窝、麻面";
    thread["defect_location"] = "左侧端部";
    return thread;
}

}  // namespace

TEST(AssembleDefectArchiveTest, GroupsObservationsUnderThreadsAndKeepsYearOrder) {
    Json::Value component;
    component["id"] = "component-1";
    Json::Value ratings(Json::arrayValue);

    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("thread-1", "蜂窝、麻面｜左侧端部"));

    Json::Value observations(Json::arrayValue);
    observations.append(make_observation("obs-2025", 2025, "thread-1"));
    observations.append(make_observation("obs-2024", 2024, "thread-1"));
    observations.append(make_observation("obs-unbound", 2025, Json::Value(Json::nullValue)));

    const auto body = assemble_defect_archive(component, ratings, threads, observations);

    ASSERT_EQ(body["threads"].size(), 1u);
    const auto& thread = body["threads"][0];
    ASSERT_EQ(thread["observations"].size(), 2u);
    // 年度倒序由输入顺序保持：线索卡片内 2025 在 2024 之前。
    EXPECT_EQ(thread["observations"][0]["id"].asString(), "obs-2025");
    EXPECT_EQ(thread["observations"][1]["id"].asString(), "obs-2024");
    ASSERT_EQ(body["unbound_observations"].size(), 1u);
    EXPECT_EQ(body["unbound_observations"][0]["id"].asString(), "obs-unbound");
    EXPECT_EQ(body["component"]["id"].asString(), "component-1");
}

TEST(AssembleDefectArchiveTest, KeepsThreadWithoutCurrentObservations) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("thread-empty", "横向裂缝｜底板跨中"));

    const auto body = assemble_defect_archive(
        Json::Value(Json::objectValue), Json::Value(Json::arrayValue), threads, Json::Value(Json::arrayValue));

    ASSERT_EQ(body["threads"].size(), 1u);
    EXPECT_TRUE(body["threads"][0]["observations"].isArray());
    EXPECT_TRUE(body["threads"][0]["observations"].empty());
    EXPECT_TRUE(body["unbound_observations"].empty());
}

TEST(AssembleDefectArchiveTest, ObservationWithUnknownThreadFallsBackToUnbound) {
    Json::Value observations(Json::arrayValue);
    observations.append(make_observation("obs-orphan", 2025, "thread-does-not-exist"));

    const auto body = assemble_defect_archive(
        Json::Value(Json::objectValue), Json::Value(Json::arrayValue), Json::Value(Json::arrayValue), observations);

    EXPECT_TRUE(body["threads"].empty());
    ASSERT_EQ(body["unbound_observations"].size(), 1u);
    EXPECT_EQ(body["unbound_observations"][0]["id"].asString(), "obs-orphan");
}

TEST(AssembleDefectArchiveTest, PassesRatingsThroughVerbatim) {
    Json::Value ratings(Json::arrayValue);
    Json::Value rating;
    rating["inspection_year"] = 2025;
    rating["score"] = 55.81;
    rating["has_validation_details"] = true;
    ratings.append(rating);

    const auto body = assemble_defect_archive(
        Json::Value(Json::objectValue), ratings, Json::Value(Json::arrayValue), Json::Value(Json::arrayValue));

    ASSERT_EQ(body["ratings"].size(), 1u);
    EXPECT_EQ(body["ratings"][0]["inspection_year"].asInt(), 2025);
    EXPECT_TRUE(body["ratings"][0]["has_validation_details"].asBool());
}
