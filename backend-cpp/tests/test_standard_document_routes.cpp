#include <string>

#include <gtest/gtest.h>

#include "bridge_report/http/RouteHelpers.hpp"
#include "bridge_report/http/StandardDocumentRoutes.hpp"

namespace {

using namespace bridge_report;

TEST(StandardDocumentRoutesTest, CreateMetadataReportsIssuesWithSectionPrefixes) {
    standards::StandardDocumentInput document;
    standards::StandardVersionInput version;
    const auto issues = http::parse_create_standard_metadata(
        R"({"document": {"name": "", "category": "技术状况评定"},)"
        R"( "version": {"label": "2011 版", "code": "JTG/T H21—2011", "kind": "official"}})",
        document, version);
    ASSERT_EQ(issues.size(), 2u);
    EXPECT_EQ(issues[0].path, "document.name");
    EXPECT_EQ(issues[1].path, "version.effective_date");
}

TEST(StandardDocumentRoutesTest, RejectsMetadataThatIsNotJson) {
    standards::StandardVersionInput version;
    const auto issues = http::parse_standard_version_metadata("not json", version);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].path, "metadata");
}

TEST(StandardDocumentRoutesTest, AcceptsADraftVersion) {
    standards::StandardVersionInput version;
    EXPECT_TRUE(http::parse_standard_version_metadata(
        R"({"label": "2026 报批稿", "code": "JTG/T 52XX—2026", "kind": "draft_approval", "publish_date": "2026-02-11"})",
        version).empty());
    EXPECT_EQ(version.kind, standards::StandardVersionKind::draft_approval);
}

TEST(StandardDocumentRoutesTest, MapsWriteConflictsToHttpErrors) {
    EXPECT_FALSE(http::standard_write_error(db::StandardDocumentWriteStatus::Ok).has_value());
    const auto taken = http::standard_write_error(db::StandardDocumentWriteStatus::NameTaken);
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(taken->code, "standard_document_name_taken");
    EXPECT_EQ(taken->status, drogon::k409Conflict);
    EXPECT_EQ(http::standard_write_error(db::StandardDocumentWriteStatus::VersionNotFound)->status,
              drogon::k404NotFound);
    EXPECT_EQ(http::standard_write_error(db::StandardDocumentWriteStatus::EffectiveDateTaken)->code,
              "standard_version_effective_date_taken");
}

TEST(StandardDocumentRoutesTest, InlineDispositionEncodesTheChineseName) {
    const auto header = http::content_disposition("inline", "JTG T H21—2011 公路.pdf", "standard.pdf");
    EXPECT_EQ(header.rfind("inline; filename=\"standard.pdf\"; filename*=UTF-8''JTG%20T%20H21%E2%80%942011%20", 0), 0u);
    EXPECT_EQ(http::attachment_disposition("a.pdf").rfind("attachment; filename=\"download\"", 0), 0u);
}

// 浏览器的 PDF 阅读器先要文件尾，再按页取；三种单段写法都得认。
TEST(StandardDocumentRoutesTest, ParsesSingleByteRanges) {
    using Kind = http::ByteRange::Kind;
    const auto head = http::parse_byte_range("bytes=0-1023", 5000);
    EXPECT_EQ(head.kind, Kind::Partial);
    EXPECT_EQ(head.offset, 0u);
    EXPECT_EQ(head.length, 1024u);

    const auto open_ended = http::parse_byte_range("bytes=4000-", 5000);
    EXPECT_EQ(open_ended.kind, Kind::Partial);
    EXPECT_EQ(open_ended.offset, 4000u);
    EXPECT_EQ(open_ended.length, 1000u);

    const auto tail = http::parse_byte_range("Bytes=-500", 5000);
    EXPECT_EQ(tail.kind, Kind::Partial);
    EXPECT_EQ(tail.offset, 4500u);
    EXPECT_EQ(tail.length, 500u);

    // 终点越过文件末尾时截到末尾。
    const auto clipped = http::parse_byte_range("bytes=4990-9999", 5000);
    EXPECT_EQ(clipped.offset, 4990u);
    EXPECT_EQ(clipped.length, 10u);
}

// 没带、看不懂、要多段：RFC 允许服务器忽略 Range，回整份文件。
TEST(StandardDocumentRoutesTest, ServesTheWholeFileForRangesItDoesNotHandle) {
    using Kind = http::ByteRange::Kind;
    EXPECT_EQ(http::parse_byte_range("", 5000).kind, Kind::Whole);
    EXPECT_EQ(http::parse_byte_range("bytes=0-99,200-299", 5000).kind, Kind::Whole);
    EXPECT_EQ(http::parse_byte_range("items=0-99", 5000).kind, Kind::Whole);
    EXPECT_EQ(http::parse_byte_range("bytes=abc-", 5000).kind, Kind::Whole);
    EXPECT_EQ(http::parse_byte_range("bytes=500-100", 5000).kind, Kind::Whole);
}

TEST(StandardDocumentRoutesTest, RefusesRangesThatStartPastTheEnd) {
    using Kind = http::ByteRange::Kind;
    EXPECT_EQ(http::parse_byte_range("bytes=5000-", 5000).kind, Kind::Unsatisfiable);
    EXPECT_EQ(http::parse_byte_range("bytes=6000-7000", 5000).kind, Kind::Unsatisfiable);
    EXPECT_EQ(http::parse_byte_range("bytes=-0", 5000).kind, Kind::Unsatisfiable);
}

}  // namespace
