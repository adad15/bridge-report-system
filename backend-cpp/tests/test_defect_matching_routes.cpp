#include <gtest/gtest.h>

#include "bridge_report/http/DefectMatchingRoutes.hpp"

namespace {

using bridge_report::rating_tree::RatingTreeMatchOutcome;
using bridge_report::review::DefectMatchRecord;
using bridge_report::review::DefectMatchReport;

TEST(DefectMatchingRoutesTest, SerializesEveryOutcomeBucketForThePage) {
    DefectMatchReport report;
    report.stats = {6, 1, 1, 1, 1, 1, 0, 1};

    DefectMatchRecord bound;
    bound.candidate_id = "d1";
    bound.outcome = RatingTreeMatchOutcome::auto_bound;
    bound.node_id = "node-water";
    bound.match_method = "controlled_keyword";
    bound.match_evidence = "命中受控关键词“渗水”。";
    report.records.push_back(bound);

    DefectMatchRecord composite;
    composite.candidate_id = "d2";
    composite.outcome = RatingTreeMatchOutcome::composite;
    composite.reason_code = "composite_defect";
    composite.reason_message = "同一条记录明确命中多个规范病害。";
    composite.candidates = {
        {"node-water", "水损", "controlled_alias", "命中别名"},
        {"node-spalling", "剥落、掉角", "controlled_keyword", "命中关键词"}};
    report.records.push_back(composite);

    DefectMatchRecord skipped;
    skipped.candidate_id = "d3";
    skipped.skipped = true;
    skipped.node_id = "node-picked";
    skipped.match_method = "manual";
    report.records.push_back(skipped);

    const auto json = bridge_report::http::defect_match_report_json(report);

    EXPECT_EQ(json["summary"]["processed"].asInt(), 6);
    EXPECT_EQ(json["summary"]["auto_bound"].asInt(), 1);
    EXPECT_EQ(json["summary"]["candidates"].asInt(), 1);
    EXPECT_EQ(json["summary"]["composite"].asInt(), 1);
    EXPECT_EQ(json["summary"]["unmatched"].asInt(), 1);
    EXPECT_EQ(json["summary"]["prerequisite_missing"].asInt(), 1);
    EXPECT_EQ(json["summary"]["failed"].asInt(), 0);
    EXPECT_EQ(json["summary"]["skipped"].asInt(), 1);

    ASSERT_EQ(json["results"].size(), 3u);
    EXPECT_EQ(json["results"][0]["outcome"].asString(), "auto_bound");
    EXPECT_EQ(json["results"][0]["rating_tree_node_id"].asString(), "node-water");
    EXPECT_EQ(json["results"][0]["match_method"].asString(), "controlled_keyword");

    // 组合病害绝不回传节点 ID，只给候选与原因。
    EXPECT_EQ(json["results"][1]["outcome"].asString(), "composite");
    EXPECT_TRUE(json["results"][1]["rating_tree_node_id"].isNull());
    EXPECT_EQ(json["results"][1]["reason_code"].asString(), "composite_defect");
    ASSERT_EQ(json["results"][1]["candidates"].size(), 2u);
    EXPECT_EQ(
        json["results"][1]["candidates"][0]["display_name"].asString(), "水损");
    EXPECT_EQ(
        json["results"][1]["candidates"][1]["match_method"].asString(),
        "controlled_keyword");

    EXPECT_TRUE(json["results"][2]["skipped"].asBool());
    EXPECT_EQ(json["results"][2]["match_method"].asString(), "manual");
}

}  // namespace
