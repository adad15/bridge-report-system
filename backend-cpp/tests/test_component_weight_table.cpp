#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bridge_report/standards/ComponentWeightTable.hpp"
#include "bridge_report/standards/StandardPackageLoader.hpp"

namespace {

using bridge_report::standards::ComponentWeightEntry;
using bridge_report::standards::StandardPackage;
using bridge_report::standards::StandardPackageLoader;
using bridge_report::standards::StructurePart;
using bridge_report::standards::component_weight_table;

// 报告的「部件权重计算表」（表4.1-1）要把本桥没有的部件也列出来并注明"无此构件"，
// 而评定结果里只有实际存在的部件。那张表的完整清单只能从规范包里取，所以这一层
// 必须原样给出规范表的行序和权重。
StandardPackage load_h21() {
    StandardPackageLoader loader;
    auto loaded = loader.load(std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
                              "standards/technical-condition/jtg-t-h21-2011/1.0.4");
    if (!loaded.ok()) throw std::runtime_error("unable to load JTG/T H21 package");
    return std::move(*loaded.package);
}

std::vector<std::string> names(const std::vector<ComponentWeightEntry>& entries) {
    std::vector<std::string> result;
    for (const auto& entry : entries) result.push_back(entry.component_type_name);
    return result;
}

TEST(ComponentWeightTableTest, ListsEveryBeamBridgeComponentInStandardOrder) {
    const auto entries = component_weight_table(load_h21(), "h21.bridge_type.beam");

    // 顺序即报告表的行序：上部三项、下部七项、桥面系六项，共 16 行。
    ASSERT_EQ(entries.size(), 16u);
    EXPECT_EQ(entries.front().structure_part, StructurePart::superstructure);
    EXPECT_EQ(entries.back().structure_part, StructurePart::deck_system);

    // 名称照规范包原文，不缩写成报告里的简称——包是唯一真源。
    const auto labels = names(entries);
    EXPECT_EQ(labels[0], "上部承重构件（主梁、挂梁）");
    EXPECT_EQ(labels[2], "支座");
    EXPECT_EQ(labels[3], "翼墙、耳墙");
    EXPECT_EQ(labels[9], "调治构造物");
    EXPECT_EQ(labels[10], "桥面铺装");
}

TEST(ComponentWeightTableTest, CarriesTheStandardWeights) {
    const auto entries = component_weight_table(load_h21(), "h21.bridge_type.beam");

    ASSERT_EQ(entries.size(), 16u);
    // 与规范原表一致：上部 0.70/0.18/0.12。
    EXPECT_DOUBLE_EQ(entries[0].configured_weight, 0.70);
    EXPECT_DOUBLE_EQ(entries[1].configured_weight, 0.18);
    EXPECT_DOUBLE_EQ(entries[2].configured_weight, 0.12);
    // 下部结构七项合计为 1。
    double lower = 0.0;
    for (const auto& entry : entries) {
        if (entry.structure_part == StructurePart::substructure) lower += entry.configured_weight;
    }
    EXPECT_NEAR(lower, 1.0, 1e-9);
}

TEST(ComponentWeightTableTest, SeparateBridgeTypesHaveSeparateTables) {
    const auto package = load_h21();

    const auto beam = component_weight_table(package, "h21.bridge_type.beam");
    const auto suspension = component_weight_table(package, "h21.bridge_type.suspension");

    EXPECT_NE(names(beam), names(suspension));
    EXPECT_EQ(names(suspension)[0], "加劲梁");
    EXPECT_EQ(names(beam)[0], "上部承重构件（主梁、挂梁）");
}

TEST(ComponentWeightTableTest, UnknownBridgeTypeYieldsNothing) {
    // 桥型不认识时给空表，而不是给一份别的桥型的权重。
    EXPECT_TRUE(component_weight_table(load_h21(), "h21.bridge_type.nope").empty());
}

}  // namespace
