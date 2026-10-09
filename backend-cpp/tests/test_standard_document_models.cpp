#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "bridge_report/standards/StandardDocumentModels.hpp"

namespace {

using namespace bridge_report::standards;

StandardVersionRecord official(const std::string& id, const std::string& effective,
                               std::optional<std::string> withdrawn = std::nullopt) {
    StandardVersionRecord version;
    version.id = id;
    version.label = id;
    version.code = "CODE-" + id;
    version.kind = StandardVersionKind::official;
    version.effective_date = effective;
    version.withdrawn_date = std::move(withdrawn);
    version.created_date = "2026-10-09";
    return version;
}

StandardVersionRecord draft(const std::string& id, StandardVersionKind kind, const std::string& publish) {
    StandardVersionRecord version;
    version.id = id;
    version.label = id;
    version.code = "CODE-" + id;
    version.kind = kind;
    version.publish_date = publish;
    version.created_date = "2026-10-09";
    return version;
}

TEST(StandardDocumentModelsTest, OfficialStatusFollowsTheEffectiveDate) {
    const std::vector<StandardVersionRecord> versions{official("v", "2011-09-01")};
    EXPECT_EQ(derive_version_states(versions, "2011-08-31")[0].status, StandardVersionStatus::upcoming);
    EXPECT_EQ(derive_version_states(versions, "2011-09-01")[0].status, StandardVersionStatus::current);
}

TEST(StandardDocumentModelsTest, LaterOfficialVersionReplacesTheEarlierOne) {
    const std::vector<StandardVersionRecord> versions{
        official("new", "2021-11-01"), official("old", "2004-10-01")};
    const auto states = derive_version_states(versions, "2026-10-09");

    EXPECT_EQ(states[0].status, StandardVersionStatus::current);
    EXPECT_EQ(states[0].replaces_version_id, std::optional<std::string>("old"));
    EXPECT_FALSE(states[0].withdrawn_date.has_value());

    EXPECT_EQ(states[1].status, StandardVersionStatus::withdrawn);
    EXPECT_EQ(states[1].withdrawn_date, std::optional<std::string>("2021-11-01"));
    EXPECT_EQ(states[1].withdrawn_date_source, std::optional<std::string>("replacement"));
    EXPECT_EQ(states[1].replaced_by_version_id, std::optional<std::string>("new"));
}

TEST(StandardDocumentModelsTest, TheWithdrawalDayItselfCountsAsWithdrawn) {
    const std::vector<StandardVersionRecord> versions{
        official("old", "2004-10-01"), official("new", "2021-11-01")};
    EXPECT_EQ(derive_version_states(versions, "2021-10-31")[0].status, StandardVersionStatus::current);
    EXPECT_EQ(derive_version_states(versions, "2021-11-01")[0].status, StandardVersionStatus::withdrawn);
}

TEST(StandardDocumentModelsTest, RemovingTheNewerVersionRestoresTheOlderOne) {
    const std::vector<StandardVersionRecord> versions{official("old", "2004-10-01")};
    const auto states = derive_version_states(versions, "2026-10-09");
    EXPECT_EQ(states[0].status, StandardVersionStatus::current);
    EXPECT_FALSE(states[0].withdrawn_date.has_value());
}

TEST(StandardDocumentModelsTest, AnUpcomingVersionLeavesTheCurrentOneInForce) {
    const std::vector<StandardVersionRecord> versions{
        official("old", "2011-09-01"), official("new", "2027-03-01")};
    const auto states = derive_version_states(versions, "2026-10-09");
    EXPECT_EQ(states[0].status, StandardVersionStatus::current);
    EXPECT_EQ(states[0].withdrawn_date, std::optional<std::string>("2027-03-01"));
    EXPECT_EQ(states[1].status, StandardVersionStatus::upcoming);
}

TEST(StandardDocumentModelsTest, AManualWithdrawalDateWinsOverTheReplacement) {
    const std::vector<StandardVersionRecord> versions{
        official("old", "2004-10-01", "2020-01-01"), official("new", "2021-11-01")};
    const auto states = derive_version_states(versions, "2026-10-09");
    EXPECT_EQ(states[0].withdrawn_date, std::optional<std::string>("2020-01-01"));
    EXPECT_EQ(states[0].withdrawn_date_source, std::optional<std::string>("manual"));
}

TEST(StandardDocumentModelsTest, DraftsKeepTheirKindAndStayOutOfTheChain) {
    const std::vector<StandardVersionRecord> versions{
        draft("approval", StandardVersionKind::draft_approval, "2026-02-11"),
        draft("comment", StandardVersionKind::draft_comment, "2017-06-01"),
        official("2011", "2011-09-01")};
    const auto states = derive_version_states(versions, "2026-10-09");
    EXPECT_EQ(states[0].status, StandardVersionStatus::draft_approval);
    EXPECT_EQ(states[1].status, StandardVersionStatus::draft_comment);
    EXPECT_EQ(states[2].status, StandardVersionStatus::current);
    EXPECT_FALSE(states[2].replaced_by_version_id.has_value());
}

TEST(StandardDocumentModelsTest, DefaultVersionPrefersCurrentThenLatest) {
    const std::vector<StandardVersionRecord> with_current{
        draft("approval", StandardVersionKind::draft_approval, "2026-02-11"), official("2011", "2011-09-01")};
    EXPECT_EQ(default_standard_version_id(with_current, derive_version_states(with_current, "2026-10-09")),
              std::optional<std::string>("2011"));

    const std::vector<StandardVersionRecord> drafts_only{
        draft("comment", StandardVersionKind::draft_comment, "2017-06-01"),
        draft("approval", StandardVersionKind::draft_approval, "2026-02-11")};
    EXPECT_EQ(default_standard_version_id(drafts_only, derive_version_states(drafts_only, "2026-10-09")),
              std::optional<std::string>("approval"));

    EXPECT_FALSE(default_standard_version_id({}, {}).has_value());
}

TEST(StandardDocumentModelsTest, DocumentJsonCarriesTheDerivedFields) {
    StandardDocumentRecord document;
    document.id = "doc";
    document.name = "公路桥涵养护规范";
    document.category = "养护与检查";
    document.versions = {official("new", "2021-11-01"), official("old", "2004-10-01")};
    document.versions[0].original_file_name = "5120.pdf";
    document.versions[0].file_size_bytes = 1234;

    const auto json = standard_document_json(document, "2026-10-09");
    EXPECT_EQ(json["default_version_id"].asString(), "new");
    EXPECT_TRUE(json["english_name"].isNull());
    EXPECT_EQ(json["versions"][0]["status"].asString(), "current");
    EXPECT_EQ(json["versions"][0]["content_path"].asString(), "/api/standard-document-versions/new/content");
    EXPECT_EQ(json["versions"][0]["file"]["size_bytes"].asUInt64(), 1234u);
    EXPECT_EQ(json["versions"][1]["status"].asString(), "withdrawn");
    EXPECT_EQ(json["versions"][1]["withdrawn_date_source"].asString(), "replacement");
    EXPECT_EQ(json["versions"][1]["replaced_by_version_id"].asString(), "new");
}

TEST(StandardDocumentModelsTest, ValidatesIsoDates) {
    EXPECT_TRUE(is_valid_iso_date("2024-02-29"));
    EXPECT_FALSE(is_valid_iso_date("2023-02-29"));
    EXPECT_FALSE(is_valid_iso_date("2011-13-01"));
    EXPECT_FALSE(is_valid_iso_date("2011/09/01"));
}

TEST(StandardDocumentModelsTest, ParsesAValidOfficialVersion) {
    Json::Value body;
    body["label"] = " 2011 版 ";
    body["code"] = "JTG/T H21—2011";
    body["kind"] = "official";
    body["publish_date"] = "2011-07-19";
    body["effective_date"] = "2011-09-01";
    body["note"] = "";
    StandardVersionInput input;
    EXPECT_TRUE(parse_standard_version_input(body, input).empty());
    EXPECT_EQ(input.label, "2011 版");
    EXPECT_EQ(input.effective_date, std::optional<std::string>("2011-09-01"));
    EXPECT_FALSE(input.note.has_value());
}

TEST(StandardDocumentModelsTest, ReportsEveryProblemWithItsPath) {
    Json::Value body;
    body["label"] = "";
    body["code"] = "JTG/T H21—2011";
    body["kind"] = "official";
    body["effective_date"] = "2011-09-01";
    body["withdrawn_date"] = "2011-08-01";
    StandardVersionInput input;
    const auto issues = parse_standard_version_input(body, input, "version.");
    ASSERT_EQ(issues.size(), 2u);
    EXPECT_EQ(issues[0].path, "version.label");
    EXPECT_EQ(issues[1].path, "version.withdrawn_date");
}

TEST(StandardDocumentModelsTest, OfficialNeedsAnEffectiveDateAndDraftsMustNotHaveOne) {
    Json::Value official_body;
    official_body["label"] = "2011 版";
    official_body["code"] = "JTG/T H21—2011";
    official_body["kind"] = "official";
    StandardVersionInput input;
    auto issues = parse_standard_version_input(official_body, input);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].path, "effective_date");

    Json::Value draft_body = official_body;
    draft_body["kind"] = "draft_approval";
    draft_body["effective_date"] = "2026-02-11";
    issues = parse_standard_version_input(draft_body, input);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].path, "effective_date");
}

TEST(StandardDocumentModelsTest, RejectsAnUnknownKindAndAnOverlongLabel) {
    Json::Value body;
    body["label"] = std::string(41, 'x');
    body["code"] = "C";
    body["kind"] = "final";
    StandardVersionInput input;
    const auto issues = parse_standard_version_input(body, input);
    ASSERT_EQ(issues.size(), 2u);
    EXPECT_EQ(issues[0].path, "label");
    EXPECT_EQ(issues[1].path, "kind");
}

TEST(StandardDocumentModelsTest, CountsCharactersNotBytes) {
    Json::Value body;
    std::string label;
    for (int index = 0; index < 40; ++index) label += "版";
    body["label"] = label;
    body["code"] = "C";
    body["kind"] = "draft_comment";
    StandardVersionInput input;
    EXPECT_TRUE(parse_standard_version_input(body, input).empty());
}

TEST(StandardDocumentModelsTest, ParsesDocumentInput) {
    Json::Value body;
    body["name"] = "公路桥梁技术状况评定标准";
    body["category"] = "技术状况评定";
    StandardDocumentInput input;
    EXPECT_TRUE(parse_standard_document_input(body, input).empty());
    EXPECT_FALSE(input.english_name.has_value());

    Json::Value missing;
    missing["name"] = "  ";
    const auto issues = parse_standard_document_input(missing, input, "document.");
    ASSERT_EQ(issues.size(), 2u);
    EXPECT_EQ(issues[0].path, "document.name");
    EXPECT_EQ(issues[1].path, "document.category");
}

TEST(StandardDocumentModelsTest, BuildsASafeDownloadFileName) {
    EXPECT_EQ(standard_pdf_file_name("JTG/T H21—2011", "公路桥梁技术状况评定标准"),
              "JTG T H21—2011 公路桥梁技术状况评定标准.pdf");
    EXPECT_EQ(standard_pdf_file_name("A:B*C", "名?称"), "A-B-C 名-称.pdf");
}

}  // namespace
