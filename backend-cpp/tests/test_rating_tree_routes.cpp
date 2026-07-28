#include <algorithm>

#include <gtest/gtest.h>

#include "bridge_report/http/RatingTreeRoutes.hpp"

TEST(RatingTreeRoutesTest, PaginationIsBoundedAndRejectsInvalidValues) {
    EXPECT_EQ(bridge_report::http::parse_rating_tree_page_limit(""), 100);
    EXPECT_EQ(bridge_report::http::parse_rating_tree_page_limit("0"), 1);
    EXPECT_EQ(bridge_report::http::parse_rating_tree_page_limit("201"), 200);
    EXPECT_EQ(bridge_report::http::parse_rating_tree_page_limit("bad"), 100);
    EXPECT_EQ(bridge_report::http::parse_rating_tree_page_offset("24"), 24);
    EXPECT_EQ(bridge_report::http::parse_rating_tree_page_offset("-1"), 0);
}

TEST(RatingTreeRoutesTest, PublicRatingTreeRoutesAreReadOnly) {
    const auto& methods = bridge_report::http::rating_tree_route_methods();
    ASSERT_EQ(methods.size(), 6u);
    EXPECT_TRUE(std::all_of(
        methods.begin(), methods.end(),
        [](const std::string& method) { return method == "GET"; }));
}
