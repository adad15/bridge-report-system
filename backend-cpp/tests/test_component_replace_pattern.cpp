#include <string>

#include <gtest/gtest.h>

#include "bridge_report/inventory/ComponentReplacePattern.hpp"

namespace {

using bridge_report::inventory::ComponentReplacePattern;

std::optional<std::string> run(
    const std::string& find, const std::string& replace, const std::string& text) {
    std::string error;
    const auto pattern = ComponentReplacePattern::compile(find, replace, error);
    EXPECT_TRUE(pattern.has_value()) << error;
    if (!pattern.has_value()) return std::nullopt;
    return pattern->apply(text);
}

}  // namespace

// 设计里的原例：报告写"第32孔桥面"，台账按编号规则生成的是"32#跨桥面铺装"。
TEST(ComponentReplacePatternTest, RewritesTheDesignExample) {
    EXPECT_EQ(run("第*孔桥面", "*#跨桥面铺装", "第32孔桥面").value_or(""),
              "32#跨桥面铺装");
    EXPECT_EQ(run("第*孔桥面", "*#跨桥面铺装", "第7孔桥面").value_or(""),
              "7#跨桥面铺装");
}

// 整串锚定：部分命中不算命中，否则"第32孔桥面板"会被悄悄当成"第32孔桥面"。
TEST(ComponentReplacePatternTest, RequiresAWholeStringMatch) {
    EXPECT_FALSE(run("第*孔桥面", "*#跨桥面铺装", "第32孔桥面板").has_value());
    EXPECT_FALSE(run("第*孔桥面", "*#跨桥面铺装", "前缀第32孔桥面").has_value());
    EXPECT_FALSE(run("第*孔桥面", "*#跨桥面铺装", "第孔桥面").has_value())
        << "* 至少要吃到一位数字";
}

TEST(ComponentReplacePatternTest, WildcardOnlyMatchesDigits) {
    EXPECT_FALSE(run("第*孔", "*#", "第三孔").has_value());
    EXPECT_EQ(run("第*孔", "*#", "第003孔").value_or(""), "003#")
        << "前导零属于报告原文，不做数值归一";
}

TEST(ComponentReplacePatternTest, SupportsSeveralWildcards) {
    EXPECT_EQ(run("*-*#板", "*跨*号板", "3-12#板").value_or(""), "3跨12号板");
    // 替换里的 * 可以少于查找：多余的捕获不使用。
    EXPECT_EQ(run("*-*#板", "*#板", "3-12#板").value_or(""), "3#板");
}

// 查找串里除 * 外一律按字面处理，用户不必了解转义。
TEST(ComponentReplacePatternTest, TreatsRegexMetacharactersLiterally) {
    EXPECT_EQ(run("(*)号", "*号", "(12)号").value_or(""), "12号");
    EXPECT_FALSE(run("(*)号", "*号", "12号").has_value());
    EXPECT_EQ(run("A.*B", "*", "A.5B").value_or(""), "5");
    EXPECT_FALSE(run("A.*B", "*", "AX5B").has_value())
        << "点号必须按字面匹配，不是正则的任意字符";
}

TEST(ComponentReplacePatternTest, RejectsAnEmptyFind) {
    std::string error;
    EXPECT_FALSE(ComponentReplacePattern::compile("", "*", error).has_value());
    EXPECT_FALSE(error.empty());
}

// 替换里多出来的 * 无从取值，属于写错而非边界情形，直接挡住。
TEST(ComponentReplacePatternTest, RejectsMoreWildcardsInReplaceThanInFind) {
    std::string error;
    EXPECT_FALSE(ComponentReplacePattern::compile("第*孔", "*-*", error).has_value());
    EXPECT_NE(error.find("*"), std::string::npos);
}

TEST(ComponentReplacePatternTest, DistinguishesNoMatchFromEmptyResult) {
    // 替换成空串是合法结果，必须与"没命中"区分开。
    const auto replaced = run("第*孔", "", "第5孔");
    ASSERT_TRUE(replaced.has_value());
    EXPECT_EQ(*replaced, "");
}
