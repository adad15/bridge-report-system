#include <string>

#include <gtest/gtest.h>

#include "bridge_report/report/ReportContextModels.hpp"

namespace {

using bridge_report::report::ReportPhoto;
using bridge_report::report::display_component_name;

// 报告里印的部件名称去掉规范名称中的举例括号（设计 §10.1）。规范包和评定结果里
// 存的仍是全称——这是印刷用词，不是事实变更。
TEST(DisplayComponentNameTest, DropsTheExampleParenthetical) {
    EXPECT_EQ(display_component_name("上部承重构件（主梁、挂梁）"), "上部承重构件");
    EXPECT_EQ(display_component_name("上部一般构件（湿接缝、横隔板等）"), "上部一般构件");
    EXPECT_EQ(display_component_name("斜拉索系统（斜拉索、锚具、拉索护套、减震装置等）"),
              "斜拉索系统");
}

TEST(DisplayComponentNameTest, LeavesPlainNamesAlone) {
    for (const auto* name : {"桥墩", "桥面铺装", "栏杆、护栏", "墩台基础", ""}) {
        EXPECT_EQ(display_component_name(name), name);
    }
}

TEST(DisplayComponentNameTest, KeepsTheWholeNameWhenItIsAllParenthetical) {
    // 截完什么都不剩时宁可印全称，也不印一个空格。
    EXPECT_EQ(display_component_name("（备用）"), "（备用）");
}

TEST(DisplayComponentNameTest, LeavesUnpairedParenthesesAlone) {
    // 括号不成对说明名称本身就长这样，不猜到哪儿截。
    EXPECT_EQ(display_component_name("桥面板（梁"), "桥面板（梁");
}

TEST(DisplayComponentNameTest, DoesNotTouchHalfWidthParentheses) {
    // 规范包里的举例一律用全角括号；半角括号出现在名称里就是名称的一部分。
    EXPECT_EQ(display_component_name("T(1)梁"), "T(1)梁");
}

// 图题：「{图号}␠␠{标题}」，两个空格，标题为空时只有图号（设计 §11.7）。
TEST(ReportPhotoTest, CaptionJoinsNumberAndTitleWithTwoSpaces) {
    ReportPhoto photo;
    photo.report_number = "照片2.1-1";
    photo.title = "1-1#板横向裂缝";

    EXPECT_EQ(photo.caption(), "照片2.1-1  1-1#板横向裂缝");
}

TEST(ReportPhotoTest, CaptionIsJustTheNumberWithoutATitle) {
    ReportPhoto photo;
    photo.report_number = "照片2.1-1";

    EXPECT_EQ(photo.caption(), "照片2.1-1");
    photo.title = "";
    EXPECT_EQ(photo.caption(), "照片2.1-1");
}

}  // namespace
