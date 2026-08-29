#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/review/ThreadSuggestions.hpp"

using bridge_report::review::normalize_suggestion_text;
using bridge_report::review::suggest_threads;
using bridge_report::review::ThreadSuggestionInput;

namespace {

// "是不是同一种病害"比评定树节点，不比文字（迁移 029）。这里默认「一个病害名称当一个
// 节点」，好让既有用例保持原意——它们本来就是拿不同的名称表示不同的病害。要显式构造
// "同名不同节点"或"异名同节点"的用例，把 node_key 传进来。
std::string node_key_for(const std::string& defect_type) {
    return "org.bridge.defect." + defect_type;
}

Json::Value make_thread(
    const std::string& id,
    const std::string& defect_type,
    const std::string& defect_location,
    int latest_seen_year = 2025,
    const std::string& system_number = "BHXS-000001",
    const std::string& node_key = ""
) {
    Json::Value thread;
    thread["id"] = id;
    thread["system_number"] = system_number;
    thread["node_key"] = node_key.empty() ? node_key_for(defect_type) : node_key;
    thread["defect_type"] = defect_type;
    thread["defect_location"] = defect_location;
    thread["latest_seen_year"] = latest_seen_year;
    return thread;
}

ThreadSuggestionInput observation_of(
    const std::string& defect_type,
    const std::string& defect_location,
    const std::string& node_key = ""
) {
    return ThreadSuggestionInput{
        node_key.empty() ? node_key_for(defect_type) : node_key,
        defect_type,
        defect_location,
    };
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

    const auto observation = observation_of("蜂窝、麻面", "左侧端部");
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

    const auto observation = observation_of("渗水泛碱", "");
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

    const auto observation = observation_of("渗水泛碱", "");
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 1u) << "同类型仍然入选，但只该拿到类型那 2 分";
    EXPECT_FALSE(suggestions[0]["match_basis"]["location_exact"].asBool());
    EXPECT_FALSE(suggestions[0]["match_basis"]["location_contains"].asBool());
    EXPECT_DOUBLE_EQ(suggestions[0]["suggestion_score"].asDouble(), 2.0);
}

TEST(SuggestThreadsTest, LocationContainmentCountsAsPartialMatch) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("t-contains", "蜂窝、麻面", "左侧端部"));

    const auto observation = observation_of("蜂窝、麻面", "左侧端部靠近0#台");
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 1u);
    EXPECT_DOUBLE_EQ(suggestions[0]["suggestion_score"].asDouble(), 2.5);
    EXPECT_FALSE(suggestions[0]["match_basis"]["location_exact"].asBool());
    EXPECT_TRUE(suggestions[0]["match_basis"]["location_contains"].asBool());
}

TEST(SuggestThreadsTest, NormalizationBridgesFullwidthAndSpacingDifferences) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("t-normalized", "蜂窝、麻面", "左侧 端部"));

    const auto observation = observation_of("蜂窝、麻面", "左侧端部");
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 1u);
    EXPECT_TRUE(suggestions[0]["match_basis"]["location_exact"].asBool());
}

TEST(SuggestThreadsTest, UnrelatedThreadsAndEmptyInputProduceNoSuggestions) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("t-unrelated", "横向裂缝", "底板跨中"));

    const auto observation = observation_of("蜂窝、麻面", "左侧端部");
    EXPECT_TRUE(suggest_threads(observation, threads).empty());
    EXPECT_TRUE(suggest_threads(observation, Json::Value(Json::arrayValue)).empty());
    EXPECT_TRUE(suggest_threads(observation, Json::Value(Json::nullValue)).empty());
}

TEST(SuggestThreadsTest, TieBreaksByLatestSeenYearThenSystemNumber) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("t-old", "蜂窝、麻面", "左侧端部", 2023, "BHXS-000001"));
    threads.append(make_thread("t-new", "蜂窝、麻面", "左侧端部", 2025, "BHXS-000002"));
    threads.append(make_thread("t-new-later-number", "蜂窝、麻面", "左侧端部", 2025, "BHXS-000003"));

    const auto observation = observation_of("蜂窝、麻面", "左侧端部");
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 3u);
    EXPECT_EQ(suggestions[0]["id"].asString(), "t-new");
    EXPECT_EQ(suggestions[1]["id"].asString(), "t-new-later-number");
    EXPECT_EQ(suggestions[2]["id"].asString(), "t-old");
}

// --- 迁移 029：推荐与归并同一口径 ----------------------------------------
//
// 推荐排序若按病害名称文字算，会与批量归组（ThreadCanonicalKey，按评定树节点）互相打架，
// 而且两个方向都错。这两条各钉一个方向。

// 报告两年都写「失效」，一年定成伸缩缝失效、一年定成支座失效。按文字这条拿满分排第一，
// 用户点了绑，后端按节点判不是同一处直接拒——推荐我绑的又不让我绑。
TEST(SuggestThreadsTest, DoesNotRecommendAThreadTheBatchFlowWouldRefuse) {
    Json::Value threads(Json::arrayValue);
    threads.append(make_thread("t-other-node", "失效", "", 2025, "BHXS-000001",
                               "org.bridge.defect.9_1_1_5"));

    const auto observation = observation_of("失效", "", "org.bridge.defect.10_2_1_4");
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 1u) << "位置都空着，仍算位置全等，所以还在候选里";
    EXPECT_FALSE(suggestions[0]["match_basis"]["same_defect_type"].asBool())
        << "文字一样不代表同一种病害";
    EXPECT_DOUBLE_EQ(suggestions[0]["suggestion_score"].asDouble(), 1.0)
        << "只剩位置分，不该拿到病害那 2 分";
}

// 反方向：「渗水、泛碱」与「渗水泛碱」是同一个节点，但归一化把顿号映射成逗号而不是删掉，
// 两串文字并不相等。按文字排，真正对的那条只拿位置分，反倒被文字恰好撞上、节点不同的
// 那条压在下面。
TEST(SuggestThreadsTest, RanksTheSameNodeFirstEvenWhenTheWordingDiffers) {
    Json::Value threads(Json::arrayValue);
    // 节点不同，但原文一字不差。
    threads.append(make_thread("t-wrong-node", "渗水泛碱", "", 2025, "BHXS-000002",
                               "org.bridge.defect.5_1_1_2"));
    // 节点相同，原文差一个顿号。
    threads.append(make_thread("t-right-node", "渗水、泛碱", "", 2024, "BHXS-000001",
                               "org.bridge.defect.5_1_1_13"));

    const auto observation = observation_of("渗水泛碱", "", "org.bridge.defect.5_1_1_13");
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 2u);
    EXPECT_EQ(suggestions[0]["id"].asString(), "t-right-node")
        << "同一个评定树病害的排前面，写法不同不影响";
    EXPECT_DOUBLE_EQ(suggestions[0]["suggestion_score"].asDouble(), 3.0);
    EXPECT_EQ(suggestions[1]["id"].asString(), "t-wrong-node");
}

// 029 之前建的线索没有 node_key。归并对这类线索本来就匹不上，推荐也不该说"同一种病害"。
TEST(SuggestThreadsTest, DoesNotTreatALegacyThreadWithoutANodeAsTheSameDefect) {
    Json::Value threads(Json::arrayValue);
    auto legacy = make_thread("t-legacy", "渗水泛碱", "");
    legacy["node_key"] = "";
    threads.append(legacy);

    const auto observation = observation_of("渗水泛碱", "");
    const auto suggestions = suggest_threads(observation, threads);

    ASSERT_EQ(suggestions.size(), 1u);
    EXPECT_FALSE(suggestions[0]["match_basis"]["same_defect_type"].asBool());
}
