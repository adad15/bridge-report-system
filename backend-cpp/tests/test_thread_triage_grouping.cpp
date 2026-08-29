#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bridge_report/review/ThreadTriageGrouping.hpp"

using bridge_report::review::build_triage_model;
using bridge_report::review::kReasonAmbiguousThread;
using bridge_report::review::kReasonLocationOverlap;
using bridge_report::review::kReasonMultipleInYear;
using bridge_report::review::TriageAction;
using bridge_report::review::TriageManualCluster;
using bridge_report::review::TriageModel;
using bridge_report::review::TriageObservationInput;
using bridge_report::review::TriageOverlapTarget;
using bridge_report::review::TriageThreadInput;

namespace {

constexpr const char* kHinge1 = "c-hinge-1";
constexpr const char* kHinge2 = "c-hinge-2";
constexpr const char* kCap16 = "c-cap-16";

// 身份的病害那一维取评定树节点（迁移 029）。这里默认「一个病害名称当一个节点」，
// 好让这个文件里既有的用例保持原意——它们本来就是拿不同的名称表示不同的病害。
// 要显式区分"同名不同节点"或"异名同节点"的用例，走下面带 node_key 的重载。
std::string node_key_for(const std::string& defect_type) {
    return "org.bridge.defect." + defect_type;
}

TriageObservationInput obs(
    const std::string& id,
    const std::string& component_id,
    int year,
    const std::string& defect_type,
    const std::string& location,
    const std::string& component_type = "铰缝",
    const std::string& structure_part = "上部结构",
    const std::string& business_code = "1#铰缝",
    const std::string& node_key = "") {
    TriageObservationInput input;
    input.id = id;
    input.bridge_component_id = component_id;
    input.structure_part = structure_part;
    input.component_type = component_type;
    input.business_component_code = business_code;
    input.node_key = node_key.empty() ? node_key_for(defect_type) : node_key;
    input.defect_type = defect_type;
    input.defect_location = location;
    input.updated_at = "2026-08-25 10:00:00+08";
    input.inspection_year = year;
    return input;
}

TriageThreadInput thread_of(
    const std::string& id,
    const std::string& component_id,
    const std::string& defect_type,
    const std::string& location,
    const std::string& system_number = "BHXS-000001") {
    TriageThreadInput input;
    input.id = id;
    input.system_number = system_number;
    input.thread_name = defect_type + "｜" + location;
    input.bridge_component_id = component_id;
    // 与 obs() 同一口径，线索才匹得上观测。
    input.node_key = node_key_for(defect_type);
    input.defect_type = defect_type;
    input.defect_location = location;
    input.updated_at = "2026-08-25 09:00:00+08";
    return input;
}

const TriageManualCluster* cluster_with(const TriageModel& model, const std::string& reason) {
    for (const auto& cluster : model.manual_clusters) {
        if (std::find(cluster.reason_codes.begin(), cluster.reason_codes.end(), reason)
            != cluster.reason_codes.end()) {
            return &cluster;
        }
    }
    return nullptr;
}

int total_groups_in_batches(const TriageModel& model) {
    int total = 0;
    for (const auto& batch : model.batches) total += static_cast<int>(batch.groups.size());
    return total;
}

}  // namespace

// 最基本的一条：同构件、同类型、同位置、连续三年 → 一条线索的候选。
TEST(ThreadTriageGroupingTest, GroupsThreeYearsOfOneDefectIntoOneGroup) {
    const auto model = build_triage_model(
        {obs("o-2024", kHinge1, 2024, "渗水泛碱", ""),
         obs("o-2026", kHinge1, 2026, "渗水泛碱", ""),
         obs("o-2025", kHinge1, 2025, "渗水泛碱", "")},
        {});

    ASSERT_EQ(model.batches.size(), 1u);
    const auto& batch = model.batches[0];
    ASSERT_EQ(batch.groups.size(), 1u);
    EXPECT_EQ(batch.action, TriageAction::Create);
    EXPECT_EQ(batch.year_set, (std::vector<int>{2024, 2025, 2026}));

    const auto& group = batch.groups[0];
    ASSERT_EQ(group.observations.size(), 3u);
    EXPECT_EQ(group.observations[0].inspection_year, 2024) << "观测必须按年度升序";
    EXPECT_EQ(group.observations[2].inspection_year, 2026);
    EXPECT_FALSE(group.matched_thread_id.has_value());
    EXPECT_TRUE(model.manual_clusters.empty());
}

// 同一年出现两条同位置同类型的记录：是两处病害还是记了两遍，只有人分得出。
// 这一组整体退出批次，绝不能混进"一次确认 166 组"的那种批里。
TEST(ThreadTriageGroupingTest, PushesAYearWithTwoObservationsIntoManualReview) {
    const auto model = build_triage_model(
        {obs("o-a", kHinge1, 2026, "渗水泛碱", ""),
         obs("o-b", kHinge1, 2026, "渗水泛碱", ""),
         obs("o-c", kHinge1, 2024, "渗水泛碱", "")},
        {});

    EXPECT_TRUE(model.batches.empty()) << "有歧义的组不许进批次";
    ASSERT_EQ(model.manual_clusters.size(), 1u);
    const auto* cluster = cluster_with(model, kReasonMultipleInYear);
    ASSERT_NE(cluster, nullptr);
    EXPECT_EQ(cluster->observation_count(), 3);
    EXPECT_EQ(model.manual_observation_count, 3);
    EXPECT_EQ(model.batchable_observation_count, 0);
}

// 位置写法逐年变化——多半是同一处病害扩展了范围，但也可能是两处。两个组都要退出批次，
// 并在同一个簇里对照着看。
TEST(ThreadTriageGroupingTest, ClustersTwoUnboundGroupsWhoseLocationsOverlap) {
    const auto model = build_triage_model(
        {obs("o-2024", kCap16, 2024, "受渗水侵蚀", "大小里程侧", "盖梁", "下部结构", "16#墩盖梁"),
         obs("o-2025", kCap16, 2025, "受渗水侵蚀", "大小里程侧及左悬臂底部", "盖梁", "下部结构", "16#墩盖梁"),
         obs("o-2026", kCap16, 2026, "受渗水侵蚀", "大小里程侧及左悬臂底部", "盖梁", "下部结构", "16#墩盖梁")},
        {});

    EXPECT_TRUE(model.batches.empty());
    ASSERT_EQ(model.manual_clusters.size(), 1u);
    const auto& cluster = model.manual_clusters[0];
    EXPECT_EQ(cluster.groups.size(), 2u) << "两个组都要退出批次，不是只退一个";
    ASSERT_FALSE(cluster.overlap_targets.empty());
    for (const auto& target : cluster.overlap_targets) {
        EXPECT_EQ(target.kind, TriageOverlapTarget::Kind::Group);
    }
}

// 增量场景的漏洞：未绑定观测写"大小里程侧"，已有线索写"大小里程侧及左悬臂底部"。
// 两者不精确相等，若 overlap 只查未绑定组之间的关系，这一组会被判成 create，
// 凭空造出一条与已有线索疑似重复的线索。
TEST(ThreadTriageGroupingTest, ClustersAnUnboundGroupOverlappingAnExistingThread) {
    const auto model = build_triage_model(
        {obs("o-2026", kCap16, 2026, "受渗水侵蚀", "大小里程侧", "盖梁", "下部结构", "16#墩盖梁")},
        {thread_of("t-1", kCap16, "受渗水侵蚀", "大小里程侧及左悬臂底部", "BHXS-000123")});

    EXPECT_TRUE(model.batches.empty()) << "不能自动 create，否则与已有线索重复";
    ASSERT_EQ(model.manual_clusters.size(), 1u);
    const auto& cluster = model.manual_clusters[0];
    ASSERT_EQ(cluster.overlap_targets.size(), 1u);
    EXPECT_EQ(cluster.overlap_targets[0].kind, TriageOverlapTarget::Kind::Thread);
    EXPECT_EQ(cluster.overlap_targets[0].system_number, "BHXS-000123");
    ASSERT_EQ(cluster.related_threads.size(), 1u);
    EXPECT_EQ(cluster.related_threads[0].id, "t-1");
}

TEST(ThreadTriageGroupingTest, ClustersAGroupMatchingMoreThanOneThread) {
    const auto model = build_triage_model(
        {obs("o-2026", kHinge1, 2026, "渗水泛碱", "")},
        {thread_of("t-1", kHinge1, "渗水泛碱", "", "BHXS-000001"),
         thread_of("t-2", kHinge1, "渗水泛碱", "", "BHXS-000002")});

    EXPECT_TRUE(model.batches.empty());
    const auto* cluster = cluster_with(model, kReasonAmbiguousThread);
    ASSERT_NE(cluster, nullptr);
    EXPECT_EQ(cluster->related_threads.size(), 2u) << "两条候选都要摆出来给人选";
}

// 空位置的组要能命中空位置的线索，否则 166 个铰缝第二年一条也绑不上。
TEST(ThreadTriageGroupingTest, BindsAnEmptyLocationGroupToItsEmptyLocationThread) {
    const auto model = build_triage_model(
        {obs("o-2026", kHinge1, 2026, "渗水泛碱", "")},
        {thread_of("t-1", kHinge1, "渗水泛碱", "", "BHXS-000001")});

    ASSERT_EQ(model.batches.size(), 1u);
    EXPECT_EQ(model.batches[0].action, TriageAction::Bind);
    ASSERT_EQ(model.batches[0].groups.size(), 1u);
    ASSERT_TRUE(model.batches[0].groups[0].matched_thread_id.has_value());
    EXPECT_EQ(*model.batches[0].groups[0].matched_thread_id, "t-1");
    EXPECT_TRUE(model.manual_clusters.empty());
}

// 线索属于具体构件：同一批里各构件绑各自那条，不存在批次级单一线索。
TEST(ThreadTriageGroupingTest, BindsEachComponentToItsOwnThreadInOneBatch) {
    const auto model = build_triage_model(
        {obs("o-1", kHinge1, 2026, "渗水泛碱", "", "铰缝", "上部结构", "1#铰缝"),
         obs("o-2", kHinge2, 2026, "渗水泛碱", "", "铰缝", "上部结构", "2#铰缝")},
        {thread_of("t-1", kHinge1, "渗水泛碱", "", "BHXS-000001"),
         thread_of("t-2", kHinge2, "渗水泛碱", "", "BHXS-000002")});

    ASSERT_EQ(model.batches.size(), 1u) << "长相相同，应当合成一批";
    const auto& batch = model.batches[0];
    ASSERT_EQ(batch.groups.size(), 2u);
    EXPECT_EQ(*batch.groups[0].matched_thread_id, "t-1");
    EXPECT_EQ(*batch.groups[1].matched_thread_id, "t-2");
}

// "三年连续"和"只有 2025 一年"是两种完全不同的判断，不该混在一屏里让人一起点。
TEST(ThreadTriageGroupingTest, SplitsBatchesByYearSet) {
    const auto model = build_triage_model(
        {obs("o-1a", kHinge1, 2024, "渗水泛碱", "", "铰缝", "上部结构", "1#铰缝"),
         obs("o-1b", kHinge1, 2025, "渗水泛碱", "", "铰缝", "上部结构", "1#铰缝"),
         obs("o-2", kHinge2, 2025, "渗水泛碱", "", "铰缝", "上部结构", "2#铰缝")},
        {});

    ASSERT_EQ(model.batches.size(), 2u);
    EXPECT_EQ(model.batches[0].observation_count(), 2) << "批次按覆盖观测数降序";
    EXPECT_EQ(model.batches[0].year_set, (std::vector<int>{2024, 2025}));
    EXPECT_EQ(model.batches[1].year_set, (std::vector<int>{2025}));
}

// "其他""附属构件"这类宽泛类型可能出现在不同结构部位，混批会让一次判断跨越两个部位。
TEST(ThreadTriageGroupingTest, SplitsBatchesByStructurePart) {
    const auto model = build_triage_model(
        {obs("o-up", "c-up", 2026, "其它病害", "", "其他", "上部结构", "1#其他"),
         obs("o-down", "c-down", 2026, "其它病害", "", "其他", "下部结构", "2#其他")},
        {});

    ASSERT_EQ(model.batches.size(), 2u);
    EXPECT_NE(model.batches[0].structure_part, model.batches[1].structure_part);
}

// batch_id 要在"读摘要"和"提交"两次请求之间对得上，所以必须跨进程稳定。
TEST(ThreadTriageGroupingTest, ProducesIdenticalIdsAndFingerprintsForIdenticalInput) {
    const std::vector<TriageObservationInput> observations{
        obs("o-1", kHinge1, 2024, "渗水泛碱", ""),
        obs("o-2", kHinge1, 2025, "渗水泛碱", "")};

    const auto first = build_triage_model(observations, {});
    const auto second = build_triage_model(observations, {});

    ASSERT_EQ(first.batches.size(), 1u);
    ASSERT_EQ(second.batches.size(), 1u);
    EXPECT_EQ(first.batches[0].batch_id, second.batches[0].batch_id);
    EXPECT_EQ(first.batches[0].fingerprint, second.batches[0].fingerprint);
    EXPECT_EQ(first.batches[0].groups[0].group_id, second.batches[0].groups[0].group_id);
    EXPECT_EQ(first.snapshot_fingerprint, second.snapshot_fingerprint);
    EXPECT_FALSE(first.batches[0].batch_id.empty());
}

// 指纹的用处就是发现"读取与提交之间数据变了"，观测变了指纹就必须变。
TEST(ThreadTriageGroupingTest, ChangesTheFingerprintWhenAnObservationChanges) {
    auto observations = std::vector<TriageObservationInput>{
        obs("o-1", kHinge1, 2024, "渗水泛碱", "")};
    const auto before = build_triage_model(observations, {});

    observations[0].updated_at = "2026-08-26 12:00:00+08";
    const auto after = build_triage_model(observations, {});

    EXPECT_NE(before.batches[0].fingerprint, after.batches[0].fingerprint);
    EXPECT_NE(before.snapshot_fingerprint, after.snapshot_fingerprint);
    EXPECT_EQ(before.batches[0].batch_id, after.batches[0].batch_id)
        << "批次身份由键决定，不该随观测内容变动";
}

// 每个组恰好落在批次或异常簇其一，不重不漏——否则会有观测整理不完却查不出在哪。
TEST(ThreadTriageGroupingTest, PartitionsEveryGroupIntoExactlyOnePlace) {
    const auto model = build_triage_model(
        {obs("o-clean", kHinge1, 2026, "渗水泛碱", "", "铰缝", "上部结构", "1#铰缝"),
         obs("o-dup-a", kHinge2, 2026, "渗水泛碱", "", "铰缝", "上部结构", "2#铰缝"),
         obs("o-dup-b", kHinge2, 2026, "渗水泛碱", "", "铰缝", "上部结构", "2#铰缝")},
        {});

    EXPECT_EQ(model.unbound_observation_count, 3);
    EXPECT_EQ(total_groups_in_batches(model), model.batchable_group_count);
    EXPECT_EQ(model.batchable_group_count + model.manual_group_count, 2);
    EXPECT_EQ(model.batchable_observation_count + model.manual_observation_count, 3);
}

TEST(ThreadTriageGroupingTest, SortsGroupsByBusinessComponentCode) {
    const auto model = build_triage_model(
        {obs("o-c", "c-3", 2026, "渗水泛碱", "", "铰缝", "上部结构", "3#铰缝"),
         obs("o-a", "c-1", 2026, "渗水泛碱", "", "铰缝", "上部结构", "1#铰缝"),
         obs("o-b", "c-2", 2026, "渗水泛碱", "", "铰缝", "上部结构", "2#铰缝")},
        {});

    ASSERT_EQ(model.batches.size(), 1u);
    const auto& groups = model.batches[0].groups;
    ASSERT_EQ(groups.size(), 3u);
    EXPECT_EQ(groups[0].observations[0].business_component_code, "1#铰缝");
    EXPECT_EQ(groups[1].observations[0].business_component_code, "2#铰缝");
    EXPECT_EQ(groups[2].observations[0].business_component_code, "3#铰缝");
}

// 位置只差空白或全角标点的两条记录是同一处病害，不该被拆成两个组。
TEST(ThreadTriageGroupingTest, GroupsAcrossPunctuationAndSpacingDifferences) {
    const auto model = build_triage_model(
        {obs("o-2024", kCap16, 2024, "受渗水侵蚀", "大小里程侧", "盖梁", "下部结构", "16#墩盖梁"),
         obs("o-2025", kCap16, 2025, "受渗水侵蚀", " 大小里程侧 ", "盖梁", "下部结构", "16#墩盖梁")},
        {});

    ASSERT_EQ(model.batches.size(), 1u);
    ASSERT_EQ(model.batches[0].groups.size(), 1u);
    EXPECT_EQ(model.batches[0].groups[0].observations.size(), 2u);
}

// --- 迁移 029：身份取评定树节点，不取病害名称文字 -------------------------
//
// 换键要解决的正是这两件事，各钉一条：文字相同不代表同一种病害，文字不同也不代表不是。

TEST(ThreadTriageGroupingTest, SplitsTheSameWordingIntoDifferentNodes) {
    // 报告两年都写「失效」，但一年解析成伸缩缝失效、一年解析成混凝土碳化——按文字会被
    // 并成一条跨年线索，按节点才分得开。「失效」「破损」这类写法在实测数据里很常见。
    std::vector<TriageObservationInput> observations{
        obs("o1", kHinge1, 2025, "失效", "", "铰缝", "上部结构", "1#铰缝",
            "org.bridge.defect.10_2_1_4"),
        obs("o2", kHinge1, 2026, "失效", "", "铰缝", "上部结构", "1#铰缝",
            "org.bridge.defect.9_1_1_5"),
    };

    const auto model = build_triage_model(std::move(observations), {});

    EXPECT_EQ(model.batchable_group_count + model.manual_group_count, 2)
        << "同一个词落在两个评定树病害上，是两处病害";
}

TEST(ThreadTriageGroupingTest, JoinsDifferentWordingUnderOneNode) {
    // 今年写「渗水、泛碱」明年写「渗水泛碱」。归一化把 `、` 映射成 `,` 而不是删掉，
    // 按文字这是两条线索；按节点是同一条。
    std::vector<TriageObservationInput> observations{
        obs("o1", kHinge1, 2025, "渗水、泛碱", "", "铰缝", "上部结构", "1#铰缝",
            "org.bridge.defect.5_1_1_13"),
        obs("o2", kHinge1, 2026, "渗水泛碱", "", "铰缝", "上部结构", "1#铰缝",
            "org.bridge.defect.5_1_1_13"),
    };

    const auto model = build_triage_model(std::move(observations), {});

    EXPECT_EQ(model.batchable_group_count + model.manual_group_count, 1)
        << "同一个评定树病害，写法不同也是同一处";
}
