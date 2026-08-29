#include <string>

#include <gtest/gtest.h>

#include "bridge_report/review/ThreadCanonicalKey.hpp"

using bridge_report::review::locations_overlap;
using bridge_report::review::make_thread_canonical_key;
using bridge_report::review::ThreadCanonicalKey;

namespace {

constexpr const char* kComponent = "11111111-1111-1111-1111-111111111111";

constexpr const char* kNode = "org.bridge.defect.5_1_1_13";

ThreadCanonicalKey key_of(const std::string& node_key, const std::string& location) {
    return make_thread_canonical_key(kComponent, node_key, location);
}

}  // namespace

// 166 个铰缝的位置字段是空的。空位置必须落到同一个规范值上，否则那批会散成 166 个孤组，
// 批量整理最大的一批直接没了。
TEST(ThreadCanonicalKeyTest, TreatsNullEmptyAndBlankLocationAsOneValue) {
    const auto empty = key_of(kNode, "");
    const auto spaces = key_of(kNode, "   ");
    const auto mixed_blank = key_of(kNode, " \t\r\n ");

    EXPECT_TRUE(empty.normalized_defect_location.empty());
    EXPECT_EQ(empty, spaces);
    EXPECT_EQ(empty, mixed_blank);
}

// 身份的病害那一维取评定树节点，不取病害名称文字（迁移 029）。
//
// 这正是换键要解决的问题：报告原文写「失效」「破损」时同一构件上分不出是哪种病害，
// 而换个年度写成「渗水、泛碱」又会和「渗水泛碱」算成两条——归一化只管全角半角与标点，
// 不认同义词，`、` 还是映射成 `,` 不是删掉。
TEST(ThreadCanonicalKeyTest, KeepsDifferentRatingTreeNodesApart) {
    const auto water = key_of("org.bridge.defect.5_1_1_13", "大小里程侧");
    const auto spalling = key_of("org.bridge.defect.5_1_1_2", "大小里程侧");

    EXPECT_FALSE(water == spalling);
}

// 节点键是受控标识符，不做文本归一化——改动它只会把上游取错了键掩盖成"匹配不上"。
TEST(ThreadCanonicalKeyTest, TakesTheNodeKeyVerbatim) {
    EXPECT_EQ(key_of(kNode, "").node_key, kNode);
    EXPECT_FALSE(key_of(kNode, "") == key_of(" org.bridge.defect.5_1_1_13 ", ""));
}

TEST(ThreadCanonicalKeyTest, KeepsDifferentComponentsApart) {
    const auto left = make_thread_canonical_key(
        "11111111-1111-1111-1111-111111111111", kNode, "");
    const auto right = make_thread_canonical_key(
        "22222222-2222-2222-2222-222222222222", kNode, "");

    EXPECT_FALSE(left == right);
}

// canonical_string 要拿去做 group_id 的哈希输入，必须确定、可分辨字段边界。
TEST(ThreadCanonicalKeyTest, SerialisesDeterministicallyAndSeparatesFields) {
    const auto key = key_of(kNode, "大小里程侧");

    EXPECT_EQ(key.canonical_string(), key.canonical_string());
    EXPECT_NE(key.canonical_string(),
              key_of(std::string(kNode) + "大小里程侧", "").canonical_string())
        << "字段拼接不能让'节点+位置'与'节点、空位置'撞成同一串";
}

TEST(LocationsOverlapTest, DetectsOneLocationContainingTheOther) {
    EXPECT_TRUE(locations_overlap("大小里程侧", "大小里程侧及左悬臂底部"));
    EXPECT_TRUE(locations_overlap("大小里程侧及左悬臂底部", "大小里程侧"));
}

// 相等是"精确匹配"，不是"重叠"——两者走完全不同的分支，混了会把干净组踢进异常簇。
TEST(LocationsOverlapTest, DoesNotCountEqualLocationsAsOverlap) {
    EXPECT_FALSE(locations_overlap("大小里程侧", "大小里程侧"));
}

// 空位置与任何位置都不算重叠：否则 166 个空位置铰缝会和同构件的一切位置纠缠在一起。
TEST(LocationsOverlapTest, IgnoresEmptyLocations) {
    EXPECT_FALSE(locations_overlap("", "大小里程侧"));
    EXPECT_FALSE(locations_overlap("大小里程侧", ""));
    EXPECT_FALSE(locations_overlap("", ""));
}

TEST(LocationsOverlapTest, DoesNotCountUnrelatedLocations) {
    EXPECT_FALSE(locations_overlap("大小里程侧", "右侧行车道"));
}
