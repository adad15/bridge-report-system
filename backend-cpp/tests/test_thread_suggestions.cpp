#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/review/ThreadSuggestions.hpp"

using bridge_report::review::normalize_suggestion_text;
using bridge_report::review::suggest_threads;
using bridge_report::review::ThreadSuggestionInput;

namespace {

Json::Value make_thread(
    const std::string& id,
    const std::string& defect_type,
    const std::string& defect_location,
    int latest_seen_year = 2025,
    const std::string& system_number = "BHXS-000001"
) {
    Json::Value thread;
    thread["id"] = id;
    thread["system_number"] = system_number;
    thread["defect_type"] = defect_type;
    thread["defect_location"] = defect_location;
    thread["latest_seen_year"] = latest_seen_year;
    return thread;
}

}  // namespace

TEST(NormalizeSuggestionTextTest, StripsWhitespaceMapsFullwidthAndLowercasesAscii) {
    EXPECT_EQ(normalize_suggestion_text("左侧 端部"), "左侧端部");
    EXPECT_EQ(normalize_suggestion_text("２－１＃板"), "2-1#板");
    EXPECT_EQ(normalize_suggestion_text("（左侧）：端部"), "(左侧):端部");
    EXPECT_EQ(normalize_suggestion_text("蜂窝、麻面。"), "蜂窝,麻面.");
    EXPECT_EQ(normalize_suggestion_text("ABC　def"), "abcdef");
}

TEST(SuggestThreadsTest, RanksTypeAndLocationMatchesAboveTypeOnly) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("t-type-only", "蜂窝、麻面", "底板跨中", 2025, "BHXS-000002"));
    threads.append(make_thread("t-type-and-location", "蜂窝、麻面", "左侧端部", 2024, "BHXS-000001"));
    threads.append(make_thread("t-location-only", "横向裂缝", "左侧端部", 2025, "BHXS-000003"));

    const ThreadSuggestionInput observation{"蜂窝、麻面", "左侧端部"};
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 3u);
    EXPECT_EQ(suggestions[0]["id"].asString(), "t-type-and-location");
    EXPECT_DOUBLE_EQ(suggestions[0]["suggestion_score"].asDouble(), 3.0);
    EXPECT_TRUE(suggestions[0]["match_basis"]["same_defect_type"].asBool());
    EXPECT_TRUE(suggestions[0]["match_basis"]["location_exact"].asBool());
    EXPECT_EQ(suggestions[1]["id"].asString(), "t-type-only");
    EXPECT_EQ(suggestions[2]["id"].asString(), "t-location-only");
    EXPECT_FALSE(suggestions[2]["match_basis"]["same_defect_type"].asBool());
}

// 铰缝这类构件本来就不写更细的位置：全桥 166 个铰缝的渗水泛碱位置字段全是空的。
// 旧实现要求"双方位置非空"才算全等，于是这 166 条在候选侧永远匹配不上——第二年拿到
// 新观测时，明明同构件同类型的线索就摆在那儿，系统一条也推不出来。
TEST(SuggestThreadsTest, TreatsTwoEmptyLocationsAsAnExactMatch) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("t-hinge", "渗水泛碱", ""));

    const ThreadSuggestionInput observation{"渗水泛碱", ""};
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 1u);
    EXPECT_EQ(suggestions[0]["id"].asString(), "t-hinge");
    EXPECT_TRUE(suggestions[0]["match_basis"]["location_exact"].asBool());
    EXPECT_DOUBLE_EQ(suggestions[0]["suggestion_score"].asDouble(), 3.0);
}

// 一侧有位置、另一侧没有，不是全等也不是包含——空位置不该和任何具体位置纠缠。
TEST(SuggestThreadsTest, DoesNotMatchAnEmptyLocationAgainstAConcreteOne) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("t-located", "渗水泛碱", "大小里程侧"));

    const ThreadSuggestionInput observation{"渗水泛碱", ""};
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 1u) << "同类型仍然入选，但只该拿到类型那 2 分";
    EXPECT_FALSE(suggestions[0]["match_basis"]["location_exact"].asBool());
    EXPECT_FALSE(suggestions[0]["match_basis"]["location_contains"].asBool());
    EXPECT_DOUBLE_EQ(suggestions[0]["suggestion_score"].asDouble(), 2.0);
}

TEST(SuggestThreadsTest, LocationContainmentCountsAsPartialMatch) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("t-contains", "蜂窝、麻面", "左侧端部"));

    const ThreadSuggestionInput observation{"蜂窝、麻面", "左侧端部靠近0#台"};
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 1u);
    EXPECT_DOUBLE_EQ(suggestions[0]["suggestion_score"].asDouble(), 2.5);
    EXPECT_FALSE(suggestions[0]["match_basis"]["location_exact"].asBool());
    EXPECT_TRUE(suggestions[0]["match_basis"]["location_contains"].asBool());
}

TEST(SuggestThreadsTest, NormalizationBridgesFullwidthAndSpacingDifferences) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("t-normalized", "蜂窝、麻面", "左侧 端部"));

    const ThreadSuggestionInput observation{"蜂窝、麻面", "左侧端部"};
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 1u);
    EXPECT_TRUE(suggestions[0]["match_basis"]["location_exact"].asBool());
}

TEST(SuggestThreadsTest, UnrelatedThreadsAndEmptyInputProduceNoSuggestions) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("t-unrelated", "横向裂缝", "底板跨中"));

    const ThreadSuggestionInput observation{"蜂窝、麻面", "左侧端部"};
    EXPECT_TRUE(suggest_threads(observation, threads).empty());
    EXPECT_TRUE(suggest_threads(observation, Json::Value(Json::arrayValue)).empty());
    EXPECT_TRUE(suggest_threads(observation, Json::Value(Json::nullValue)).empty());
}

TEST(SuggestThreadsTest, TieBreaksByLatestSeenYearThenSystemNumber) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("t-old", "蜂窝、麻面", "左侧端部", 2023, "BHXS-000001"));
    threads.append(make_thread("t-new", "蜂窝、麻面", "左侧端部", 2025, "BHXS-000002"));
    threads.append(make_thread("t-new-later-number", "蜂窝、麻面", "左侧端部", 2025, "BHXS-000003"));

    const ThreadSuggestionInput observation{"蜂窝、麻面", "左侧端部"};
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 3u);
    EXPECT_EQ(suggestions[0]["id"].asString(), "t-new");
    EXPECT_EQ(suggestions[1]["id"].asString(), "t-new-later-number");
    EXPECT_EQ(suggestions[2]["id"].asString(), "t-old");
}
