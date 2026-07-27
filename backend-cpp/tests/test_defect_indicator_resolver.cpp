#include <gtest/gtest.h>

#include "bridge_report/standards/DefectIndicatorResolver.hpp"
#include "support/h21_fixtures.hpp"

namespace standards = bridge_report::standards;

TEST(DefectIndicatorResolverTest, ResolvesApplicableIndicatorAndScale) {
    const auto package = bridge_report::tests::h21::load_package();
    const auto result = standards::resolve_defect_indicator(
        package, "h21.defect.5_3_1_1", "h21.component.bearing", 2);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.indicator_name, "板式支座老化变质、开裂");
    EXPECT_EQ(result.allowed_scales, (std::vector<int>{1, 2, 3, 4, 5}));
}

TEST(DefectIndicatorResolverTest, ReturnsStableFailureKinds) {
    const auto package = bridge_report::tests::h21::load_package();
    EXPECT_EQ(
        standards::resolve_defect_indicator(
            package, "", "h21.component.bearing", 2).status,
        standards::DefectIndicatorResolutionStatus::indicator_required);
    EXPECT_EQ(
        standards::resolve_defect_indicator(
            package, "missing", "h21.component.bearing", 2).status,
        standards::DefectIndicatorResolutionStatus::indicator_unknown);
    EXPECT_EQ(
        standards::resolve_defect_indicator(
            package, "h21.defect.5_3_1_1", "h21.component.deck_pavement", 2).status,
        standards::DefectIndicatorResolutionStatus::indicator_not_applicable);
    EXPECT_EQ(
        standards::resolve_defect_indicator(
            package, "h21.defect.5_3_1_1", "h21.component.bearing", 8).status,
        standards::DefectIndicatorResolutionStatus::scale_not_allowed);
}
