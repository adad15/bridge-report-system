#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <map>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/review/ThreadSuggestions.hpp"
#include "bridge_report/review/ThreadTriageGrouping.hpp"
#include "bridge_report/review/TriageSummaryJson.hpp"

using bridge_report::review::build_triage_model;
using bridge_report::review::pick_sample_groups;
using bridge_report::review::triage_summary_json;
using bridge_report::review::TriageModel;
using bridge_report::review::TriageObservationInput;
using bridge_report::review::TriageThreadInput;

namespace {

TriageObservationInput obs(
    const std::string& id, const std::string& component_id, int year,
    const std::string& business_code, const std::string& location = "") {
    TriageObservationInput input;
    input.id = id;
    input.bridge_component_id = component_id;
    input.structure_part = "上部结构";
    input.component_type = "铰缝";
    input.business_component_code = business_code;
    input.defect_type = "渗水泛碱";
    input.defect_location = location;
    input.updated_at = "2026-08-26 10:00:00+08";
    input.inspection_year = year;
    return input;
}

/// count 个构件，各自三年，长相相同 → 一个批次。
TriageModel model_with(int count) {
    std::vector<TriageObservationInput> observations;
    for (int index = 1; index <= count; ++index) {
        const auto component = "c-" + std::to_string(index);
        const auto code = std::to_string(index) + "#铰缝";
        for (const auto year : {2024, 2025, 2026}) {
            observations.push_back(
                obs(component + "-" + std::to_string(year), component, year, code));
        }
    }
    return build_triage_model(std::move(observations), {});
}

}  // namespace

// 摘要绝不能带上批次的全部观测：最大的一批就有 489 条，一次性推过去正是现有页面的死法。
TEST(TriageSummaryJsonTest, LeavesTheBatchObservationsOutOfTheSummary) {
    const auto body = triage_summary_json(model_with(20));

    ASSERT_EQ(body["batches"].size(), 1u);
    const auto& batch = body["batches"][0];
    EXPECT_EQ(batch["group_count"].asInt(), 20);
    EXPECT_EQ(batch["observation_count"].asInt(), 60);
    EXPECT_FALSE(batch.isMember("groups")) << "全部组只能由明细接口给";
    EXPECT_EQ(batch["sample_groups"].size(), 3u);
}

// 取头、中、尾而不是前三条：163 个铰缝只看 1#/2#/3# 看不出这批横跨全桥。
TEST(TriageSummaryJsonTest, SamplesTheHeadMiddleAndTailOfTheBatch) {
    const auto model = model_with(9);
    ASSERT_EQ(model.batches.size(), 1u);

    const auto samples = pick_sample_groups(model.batches[0]);

    ASSERT_EQ(samples.size(), 3u);
    EXPECT_EQ(samples[0]->group_id, model.batches[0].groups.front().group_id);
    EXPECT_EQ(samples[2]->group_id, model.batches[0].groups.back().group_id);
    EXPECT_NE(samples[1]->group_id, samples[0]->group_id);
    EXPECT_NE(samples[1]->group_id, samples[2]->group_id);
}

TEST(TriageSummaryJsonTest, ReturnsEveryGroupWhenTheBatchIsTiny) {
    const auto model = model_with(2);

    EXPECT_EQ(pick_sample_groups(model.batches[0]).size(), 2u);
}

// 刷新一次样例换一批，人就没法复核自己刚看过什么。
TEST(TriageSummaryJsonTest, PicksTheSameSamplesEveryTime) {
    const auto first = triage_summary_json(model_with(30));
    const auto second = triage_summary_json(model_with(30));

    ASSERT_EQ(first["batches"][0]["sample_groups"].size(), 3u);
    for (Json::ArrayIndex index = 0; index < 3; ++index) {
        EXPECT_EQ(first["batches"][0]["sample_groups"][index]["group_id"].asString(),
                  second["batches"][0]["sample_groups"][index]["group_id"].asString());
    }
    EXPECT_EQ(first["snapshot_id"].asString(), second["snapshot_id"].asString());
}

// 空位置要序列化成 null，不能是空字符串——数据库那侧存的就是 null，两边口径得一致。
TEST(TriageSummaryJsonTest, SerialisesAnAbsentLocationAsNull) {
    const auto body = triage_summary_json(model_with(4));

    EXPECT_TRUE(body["batches"][0]["defect_location"].isNull());
}

// 异常簇相反：总共十来组，必须给全上下文，人才判断得出该合还是该分。
TEST(TriageSummaryJsonTest, GivesManualClustersTheirFullContext) {
    std::vector<TriageObservationInput> observations{
        obs("o-2024", "c-cap", 2024, "16#墩盖梁", "大小里程侧"),
        obs("o-2025", "c-cap", 2025, "16#墩盖梁", "大小里程侧及左悬臂底部")};
    TriageThreadInput thread;
    thread.id = "t-1";
    thread.system_number = "BHXS-000123";
    thread.thread_name = "渗水泛碱｜大小里程侧及左悬臂底部";
    thread.bridge_component_id = "c-cap";
    thread.defect_type = "渗水泛碱";
    thread.defect_location = "大小里程侧及左悬臂底部";

    const auto body = triage_summary_json(build_triage_model(observations, {thread}));

    ASSERT_EQ(body["manual_clusters"].size(), 1u);
    const auto& cluster = body["manual_clusters"][0];
    EXPECT_EQ(cluster["reason_codes"][0].asString(), "location_overlap");
    ASSERT_FALSE(cluster["groups"].empty());
    EXPECT_FALSE(cluster["groups"][0]["observations"].empty())
        << "异常簇的组必须带历年观测，否则人没法对照";
    ASSERT_FALSE(cluster["overlap_targets"].empty());

    bool saw_thread_target = false;
    for (const auto& target : cluster["overlap_targets"]) {
        if (target["kind"].asString() != "thread") continue;
        saw_thread_target = true;
        EXPECT_EQ(target["system_number"].asString(), "BHXS-000123");
    }
    EXPECT_TRUE(saw_thread_target) << "与已有线索重叠时必须标出是线索并带 BHXS 编号";
}

// 人要判断"这几条是不是同一处病害"，判据是标度、尺寸和照片——只给位置写法和病害类型，
// 等于把系统已经判不了的那个信号原样还给人看一遍。异常簇的观测必须带上展示字段。
TEST(TriageSummaryJsonTest, GivesManualClusterObservationsTheirEvidenceFields) {
    std::vector<TriageObservationInput> observations{
        obs("o-2024", "c-cap", 2024, "16#墩盖梁", "大小里程侧"),
        obs("o-2025", "c-cap", 2025, "16#墩盖梁", "大小里程侧及左悬臂底部")};

    bridge_report::review::TriageDisplayLookup display;
    auto& first = display["o-2024"];
    first.system_number = "BH-000412";
    first.scale = "2";
    first.description = "大小里程侧渗水泛碱";
    first.measurements = {"0.3mm×2.0m"};
    first.photos = {{"p-1", "2.3-14"}};
    // 第二条没有尺寸也没有照片：缺证据是正常数据，不能让它整条消失。
    auto& second = display["o-2025"];
    second.system_number = "BH-000533";
    second.scale = "3";
    second.description = "渗水泛碱范围扩大";

    const auto body = triage_summary_json(build_triage_model(observations, {}), display);

    ASSERT_EQ(body["manual_clusters"].size(), 1u);
    std::map<std::string, Json::Value> by_id;
    for (const auto& group : body["manual_clusters"][0]["groups"]) {
        for (const auto& observation : group["observations"]) {
            by_id[observation["id"].asString()] = observation;
        }
    }
    ASSERT_EQ(by_id.count("o-2024"), 1u);
    ASSERT_EQ(by_id.count("o-2025"), 1u);

    const auto& carrying = by_id["o-2024"];
    EXPECT_EQ(carrying["system_number"].asString(), "BH-000412");
    EXPECT_EQ(carrying["scale"].asString(), "2");
    ASSERT_EQ(carrying["measurements"].size(), 1u);
    EXPECT_EQ(carrying["measurements"][0].asString(), "0.3mm×2.0m");
    ASSERT_EQ(carrying["photos"].size(), 1u);
    EXPECT_EQ(carrying["photos"][0]["photo_number"].asString(), "2.3-14");

    const auto& bare = by_id["o-2025"];
    EXPECT_EQ(bare["scale"].asString(), "3");
    EXPECT_TRUE(bare["measurements"].empty()) << "缺尺寸要给空数组，不是缺键";
    EXPECT_TRUE(bare["photos"].empty());
}

// 明细不分页的前提是"它确实不大"。设计里那个 150–250 KB 是估算，这里量实的：
// 最大批次 163 组 / 489 条，只含 JSON 元数据（照片只给 id，二进制走既有内容接口）。
// 数字若涨到接近 1 MB，说明该重新考虑分页——但那时得连"跨页剔除如何与提交清单一致"
// 一起解决，不能只把接口切开。
TEST(TriageSummaryJsonTest, KeepsTheLargestBatchDetailSmallEnoughToSkipPaging) {
    const auto path = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT)
        / "backend-cpp" / "tests" / "fixtures" / "baigu_triage_snapshot.json";
    std::ifstream input(path);
    ASSERT_TRUE(input.is_open());
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    ASSERT_TRUE(Json::parseFromStream(builder, input, &root, &errors)) << errors;

    std::vector<TriageObservationInput> observations;
    for (const auto& item : root["observations"]) {
        TriageObservationInput observation;
        observation.id = item["id"].asString();
        observation.bridge_component_id = item["bridge_component_id"].asString();
        observation.structure_part = item["structure_part"].asString();
        observation.component_type = item["component_type"].asString();
        observation.business_component_code = item["business_component_code"].asString();
        observation.defect_type = item["defect_type"].asString();
        // 快照早于评定树解析，没有 node_key：用归一化后的病害名称顶替，一个名称当一个
        // 节点。详见 test_thread_triage_fixture.cpp 的 node_key_of()。
        observation.node_key =
            bridge_report::review::normalize_suggestion_text(observation.defect_type);
        observation.defect_location = item["defect_location"].asString();
        observation.updated_at = item["updated_at"].asString();
        observation.inspection_year = item["inspection_year"].asInt();
        observations.push_back(std::move(observation));
    }
    const auto model = build_triage_model(std::move(observations), {});
    ASSERT_FALSE(model.batches.empty());
    const auto& largest = model.batches.front();

    // 只算归组模型能给出的部分（id、令牌、年份、类型、位置）；标度尺寸照片由仓储另取，
    // 那部分体积与观测数同阶，这里的量级判断已足够说明问题。
    Json::Value detail;
    detail["batch_id"] = largest.batch_id;
    detail["groups"] = Json::Value(Json::arrayValue);
    for (const auto& group : largest.groups) {
        Json::Value group_json;
        group_json["group_id"] = group.group_id;
        group_json["observations"] = Json::Value(Json::arrayValue);
        for (const auto& observation : group.observations) {
            Json::Value entry;
            entry["id"] = observation.id;
            entry["updated_at"] = observation.updated_at;
            entry["inspection_year"] = observation.inspection_year;
            entry["defect_type"] = observation.defect_type;
            entry["defect_location"] = observation.defect_location;
            group_json["observations"].append(entry);
        }
        detail["groups"].append(group_json);
    }

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    const auto serialised = Json::writeString(writer, detail);
    EXPECT_EQ(largest.groups.size(), 163u);
    EXPECT_EQ(largest.observation_count(), 489);
    EXPECT_LT(serialised.size(), 512u * 1024u)
        << "最大批次明细骨架 " << serialised.size() << " 字节";
    std::cout << "[量测] 最大批次明细骨架：" << serialised.size() << " 字节（163 组 / 489 条）" << std::endl;
}

// 真实数据过一遍：摘要体积必须与批次数同量级，而不是与 1197 条观测同量级。
TEST(TriageSummaryJsonTest, KeepsTheBaiguSummarySmall) {
    const auto path = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT)
        / "backend-cpp" / "tests" / "fixtures" / "baigu_triage_snapshot.json";
    std::ifstream input(path);
    ASSERT_TRUE(input.is_open());
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    ASSERT_TRUE(Json::parseFromStream(builder, input, &root, &errors)) << errors;

    std::vector<TriageObservationInput> observations;
    for (const auto& item : root["observations"]) {
        TriageObservationInput observation;
        observation.id = item["id"].asString();
        observation.bridge_component_id = item["bridge_component_id"].asString();
        observation.structure_part = item["structure_part"].asString();
        observation.component_type = item["component_type"].asString();
        observation.business_component_code = item["business_component_code"].asString();
        observation.defect_type = item["defect_type"].asString();
        // 快照早于评定树解析，没有 node_key：用归一化后的病害名称顶替，一个名称当一个
        // 节点。详见 test_thread_triage_fixture.cpp 的 node_key_of()。
        observation.node_key =
            bridge_report::review::normalize_suggestion_text(observation.defect_type);
        observation.defect_location = item["defect_location"].asString();
        observation.updated_at = item["updated_at"].asString();
        observation.inspection_year = item["inspection_year"].asInt();
        observations.push_back(std::move(observation));
    }

    const auto body = triage_summary_json(build_triage_model(std::move(observations), {}));
    EXPECT_EQ(body["batches"].size(), 144u);
    EXPECT_EQ(body["unbound_observation_count"].asInt(), 1197);

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    const auto serialised = Json::writeString(writer, body);
    EXPECT_LT(serialised.size(), 200u * 1024u)
        << "摘要 " << serialised.size() << " 字节——它该随批次数走，不该随观测数走";
}
