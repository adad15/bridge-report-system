#include <gtest/gtest.h>

#include <vector>

#include "bridge_report/inventory/ComponentCategoryLexicon.hpp"

namespace nt = bridge_report::inventory;

TEST(CategoryLexiconTest, MapsRegulationNamesToCategory) {
    EXPECT_EQ(nt::resolve_component_categories("上部承重构件"),
              (std::vector<std::string>{"h21.component.beam.upper_bearing"}));
    EXPECT_EQ(nt::resolve_component_categories("支座"),
              (std::vector<std::string>{"h21.component.bearing"}));
    // 斜拉桥上部承重的规范名是"主梁"，与梁式桥的"上部承重构件"不同名，故单候选。
    EXPECT_EQ(nt::resolve_component_categories("主梁"),
              (std::vector<std::string>{"h21.component.cable_stayed.main_girder"}));
}

TEST(CategoryLexiconTest, MapsSourceDrainageAndRiverbedNamesToCategory) {
    // 来源离线库把具体构件“排水系统”的父级类别写成“防排水系统”；
    // 两种名称都必须落到同一个 H21 类别，才能与台账编号继续做精确匹配。
    EXPECT_EQ(nt::resolve_component_categories("防排水系统"),
              (std::vector<std::string>{"h21.component.deck.drainage"}));
    EXPECT_EQ(nt::resolve_component_categories("排水系统"),
              (std::vector<std::string>{"h21.component.deck.drainage"}));
    EXPECT_EQ(nt::resolve_component_categories("河床"),
              (std::vector<std::string>{"h21.component.lower.riverbed"}));
}

TEST(CategoryLexiconTest, CrossBridgeTypeDuplicateNamesReturnCandidates) {
    EXPECT_EQ(nt::resolve_component_categories("横向联结系"),
              (std::vector<std::string>{"h21.component.arch.transverse_link",
                                        "h21.component.composite_arch.transverse_link"}));
    EXPECT_EQ(nt::resolve_component_categories("索塔"),
              (std::vector<std::string>{"h21.component.cable_stayed.tower",
                                        "h21.component.suspension.tower"}));
    EXPECT_EQ(nt::resolve_component_categories("桥面板"),
              (std::vector<std::string>{"h21.component.arch.deck_slab",
                                        "h21.component.composite_arch.deck_slab_or_beam"}));
}

TEST(CategoryLexiconTest, NormalizesWhitespaceAndParentheticalExamples) {
    EXPECT_EQ(nt::resolve_component_categories(" 上部承重构件（主梁、挂梁） "),
              (std::vector<std::string>{"h21.component.beam.upper_bearing"}));
    // 刚架拱片的括号内是别名"桁架拱片"，两种写法都归到同一类别。
    EXPECT_EQ(nt::resolve_component_categories("桁架拱片"),
              (std::vector<std::string>{"h21.component.arch.rigid_or_truss_segment"}));
}

TEST(CategoryLexiconTest, UnknownNameYieldsNoCategory) {
    EXPECT_TRUE(nt::resolve_component_categories("不存在的部件").empty());
    EXPECT_TRUE(nt::resolve_component_categories("").empty());
}
