#include <string>

#include <gtest/gtest.h>

#include "bridge_report/review/ThreadCanonicalKey.hpp"

using bridge_report::review::locations_overlap;
using bridge_report::review::make_thread_canonical_key;
using bridge_report::review::ThreadCanonicalKey;

namespace {

constexpr const char* kComponent = "11111111-1111-1111-1111-111111111111";

ThreadCanonicalKey key_of(const std::string& defect_type, const std::string& location) {
    return make_thread_canonical_key(kComponent, defect_type, location);
}

}  // namespace

// 166 个铰缝的位置字段是空的。空位置必须落到同一个规范值上，否则那批会散成 166 个孤组，
// 批量整理最大的一批直接没了。
TEST(ThreadCanonicalKeyTest, TreatsNullEmptyAndBlankLocationAsOneValue) {
    const auto empty = key_of("渗水泛碱", "");
    const auto spaces = key_of("渗水泛碱", "   ");
    const auto mixed_blank = key_of("渗水泛碱", " \t\r\n ");

    EXPECT_TRUE(empty.normalized_defect_location.empty());
    EXPECT_EQ(empty, spaces);
    EXPECT_EQ(empty, mixed_blank);
}

// 候选算法一直对病害类型也做规范化；组键若只规范化位置，就会出现"候选说是同一处、
// 批次说不是"的分裂。这条锁住类型同样走规范化。
TEST(ThreadCanonicalKeyTest, NormalisesTheDefectTypeAsWellAsTheLocation) {
    const auto plain = key_of("渗水泛碱", "大小里程侧");
    const auto spaced = key_of(" 渗水泛碱 ", "大小里程侧");
    const auto fullwidth = key_of("渗水泛碱（Ａ）", "大小里程侧");
    const auto halfwidth_upper = key_of("渗水泛碱(A)", "大小里程侧");

    EXPECT_EQ(plain, spaced);
    EXPECT_EQ(fullwidth, halfwidth_upper) << "全角括号与大写字母都该被规范掉";
}

TEST(ThreadCanonicalKeyTest, KeepsDifferentComponentsApart) {
    const auto left = make_thread_canonical_key(
        "11111111-1111-1111-1111-111111111111", "渗水泛碱", "");
    const auto right = make_thread_canonical_key(
        "22222222-2222-2222-2222-222222222222", "渗水泛碱", "");

    EXPECT_FALSE(left == right);
}

// canonical_string 要拿去做 group_id 的哈希输入，必须确定、可分辨字段边界。
TEST(ThreadCanonicalKeyTest, SerialisesDeterministicallyAndSeparatesFields) {
    const auto key = key_of("渗水泛碱", "大小里程侧");

    EXPECT_EQ(key.canonical_string(), key.canonical_string());
    EXPECT_NE(key.canonical_string(), key_of("渗水泛碱大小里程侧", "").canonical_string())
        << "字段拼接不能让'类型+位置'与'类型、空位置'撞成同一串";
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
