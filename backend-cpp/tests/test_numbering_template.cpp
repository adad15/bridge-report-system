#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bridge_report/inventory/NumberingTemplate.hpp"

namespace nt = bridge_report::inventory;

TEST(NumberingRangeTest, SkeletonRangesFollowTopology) {
    const auto ctx = nt::NumberingContext{/*span_count=*/5};
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Span, 0),
              (std::vector<nt::PlaceValue>{{"1", "第1孔"}, {"2", "第2孔"}, {"3", "第3孔"},
                                           {"4", "第4孔"}, {"5", "第5孔"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Pier, 0),
              (std::vector<nt::PlaceValue>{{"1", "第1墩"}, {"2", "第2墩"},
                                           {"3", "第3墩"}, {"4", "第4墩"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Abutment, 0),
              (std::vector<nt::PlaceValue>{{"0", "第0台"}, {"5", "第5台"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::SupportLine, 0),
              (std::vector<nt::PlaceValue>{{"0#台", "0#台"}, {"1#墩", "1#墩"}, {"2#墩", "2#墩"},
                                           {"3#墩", "3#墩"}, {"4#墩", "4#墩"}, {"5#台", "5#台"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Side, 0),
              (std::vector<nt::PlaceValue>{{"左", "左侧"}, {"右", "右侧"}}));
    EXPECT_EQ(nt::placeholder_values(ctx, nt::Placeholder::Count, 3),
              (std::vector<nt::PlaceValue>{{"1", ""}, {"2", ""}, {"3", ""}}));
}
