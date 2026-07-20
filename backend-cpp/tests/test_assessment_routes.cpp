#include <gtest/gtest.h>

#include "bridge_report/http/AssessmentRoutes.hpp"

TEST(AssessmentRoutesTest, ParsesDraftAndIntegerClientRevisionWithoutRatingAuthority) {
    Json::Value body;
    body["client_revision"] = 12;
    body["draft"]["defects"] = Json::Value(Json::arrayValue);
    body["draft"]["ratings"]["overall"]["total_score"] = 42.0;

    bridge_report::assessment::AssessmentPreviewPayload payload;
    ASSERT_TRUE(bridge_report::http::parse_assessment_preview_request(body, payload));
    EXPECT_EQ(payload.client_revision, 12);
    EXPECT_TRUE(payload.draft["defects"].isArray());
}

TEST(AssessmentRoutesTest, RejectsMissingDraftAndCoercedRevision) {
    bridge_report::assessment::AssessmentPreviewPayload payload;
    Json::Value missing_draft;
    missing_draft["client_revision"] = 1;
    EXPECT_FALSE(bridge_report::http::parse_assessment_preview_request(missing_draft, payload));

    Json::Value coerced;
    coerced["client_revision"] = "1";
    coerced["draft"] = Json::Value(Json::objectValue);
    EXPECT_FALSE(bridge_report::http::parse_assessment_preview_request(coerced, payload));
}
