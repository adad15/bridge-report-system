#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/review/ThreadTriageGrouping.hpp"

using bridge_report::review::build_triage_model;
using bridge_report::review::kReasonAmbiguousThread;
using bridge_report::review::kReasonLocationOverlap;
using bridge_report::review::kReasonMultipleInYear;
using bridge_report::review::TriageAction;
using bridge_report::review::TriageModel;
using bridge_report::review::TriageObservationInput;
using bridge_report::review::TriageThreadInput;

namespace {

// 百股大桥 2024/2025/2026 三个已确认年度的全部未绑定观测，导出于 2026-08-26。
// 夹具头部带 content_sha256 与来源导入记录号：数据快照换了才好分清"是数据变了"
// 还是"是归组规则变了"——只有数量断言的话，红了也追不回原因。
std::filesystem::path fixture_path() {
    return std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT)
        / "backend-cpp" / "tests" / "fixtures" / "baigu_triage_snapshot.json";
}

Json::Value load_fixture() {
    std::ifstream input(fixture_path());
    EXPECT_TRUE(input.is_open()) << "缺少夹具：" << fixture_path().string();
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    EXPECT_TRUE(Json::parseFromStream(builder, input, &root, &errors)) << errors;
    return root;
}

std::string text(const Json::Value& value, const char* key) {
    return value[key].isString() ? value[key].asString() : std::string();
}

TriageModel model_from_fixture(const Json::Value& root) {
    std::vector<TriageObservationInput> observations;
    observations.reserve(root["observations"].size());
    for (const auto& item : root["observations"]) {
        TriageObservationInput observation;
        observation.id = text(item, "id");
        observation.bridge_component_id = text(item, "bridge_component_id");
        observation.structure_part = text(item, "structure_part");
        observation.component_type = text(item, "component_type");
        observation.business_component_code = text(item, "business_component_code");
        observation.defect_type = text(item, "defect_type");
        observation.defect_location = text(item, "defect_location");
        observation.updated_at = text(item, "updated_at");
        observation.inspection_year = item["inspection_year"].asInt();
        observations.push_back(std::move(observation));
    }

    std::vector<TriageThreadInput> threads;
    threads.reserve(root["threads"].size());
    for (const auto& item : root["threads"]) {
        TriageThreadInput thread;
        thread.id = text(item, "id");
        thread.system_number = text(item, "system_number");
        thread.thread_name = text(item, "thread_name");
        thread.bridge_component_id = text(item, "bridge_component_id");
        thread.defect_type = text(item, "defect_type");
        thread.defect_location = text(item, "defect_location");
        thread.updated_at = text(item, "updated_at");
        threads.push_back(std::move(thread));
    }

    return build_triage_model(std::move(observations), std::move(threads));
}

int clusters_with(const TriageModel& model, const std::string& reason) {
    int groups = 0;
    for (const auto& cluster : model.manual_clusters) {
        for (const auto& code : cluster.reason_codes) {
            if (code != reason) continue;
            groups += static_cast<int>(cluster.groups.size());
            break;
        }
    }
    return groups;
}

}  // namespace

TEST(ThreadTriageFixtureTest, CarriesProvenanceSoDataDriftIsTraceable) {
    const auto root = load_fixture();
    const auto& provenance = root["provenance"];

    EXPECT_EQ(provenance["observation_count"].asInt(), 1197);
    EXPECT_EQ(provenance["thread_count"].asInt(), 0) << "冷启动快照：线索为 0";
    EXPECT_EQ(text(provenance, "source_import_records"),
              "DRJL-000013,DRJL-000014,DRJL-000015");
    EXPECT_EQ(text(provenance, "content_sha256").size(), 64u);
    EXPECT_EQ(root["observations"].size(), 1197u);
}

// 设计 §12.5 的基线。归组口径将来但凡改动，这条立刻变红，改的人必须解释清楚为什么。
TEST(ThreadTriageFixtureTest, ReproducesTheBaiguGroupingBaseline) {
    const auto model = model_from_fixture(load_fixture());

    EXPECT_EQ(model.unbound_observation_count, 1197);
    EXPECT_EQ(model.batchable_group_count + model.manual_group_count, 491)
        << "总组数：491";
    EXPECT_EQ(model.batchable_group_count, 481);
    EXPECT_EQ(model.batchable_observation_count, 1174);
    EXPECT_EQ(model.batches.size(), 144u);

    EXPECT_EQ(clusters_with(model, kReasonMultipleInYear), 3);
    EXPECT_EQ(clusters_with(model, kReasonLocationOverlap), 7);
    EXPECT_EQ(clusters_with(model, kReasonAmbiguousThread), 0)
        << "冷启动没有已有线索，不可能命中多条";
    EXPECT_EQ(model.batchable_observation_count + model.manual_observation_count, 1197)
        << "每条观测恰好落在批次或异常簇其一，不重不漏";
}

// 决策数从 1197 降到 144 靠的就是这一批：一次判断覆盖 163 个铰缝、489 条观测。
//
// 这个形状下总共有 166 个组、503 条观测，其中 3 组某年记了两条（共 14 条观测）被挡在
// 批次外——设计初稿写的"166 组 / 498 条"是拿排除异常前的口径算的，正是这条夹具把它
// 逮出来的。异常必须先出局，批次里才只剩语义单一的组。
TEST(ThreadTriageFixtureTest, PutsTheHingeJointSeepageInOneBatchOf163) {
    const auto model = model_from_fixture(load_fixture());

    ASSERT_FALSE(model.batches.empty());
    const auto& largest = model.batches.front();
    EXPECT_EQ(largest.component_type, "铰缝");
    EXPECT_EQ(largest.defect_type, "渗水泛碱");
    EXPECT_TRUE(largest.defect_location.empty()) << "铰缝不写更细位置";
    EXPECT_EQ(largest.year_set, (std::vector<int>{2024, 2025, 2026}));
    EXPECT_EQ(largest.groups.size(), 163u);
    EXPECT_EQ(largest.observation_count(), 489);
}

TEST(ThreadTriageFixtureTest, MakesEveryBatchACreateOnAColdStart) {
    const auto model = model_from_fixture(load_fixture());

    for (const auto& batch : model.batches) {
        EXPECT_EQ(batch.action, TriageAction::Create)
            << "线索为 0，不可能有 bind：" << batch.component_type;
    }
}
