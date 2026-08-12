#include <gtest/gtest.h>

#include <fstream>

#include "bridge_report/archive/SourceDbReference.hpp"

namespace {

using bridge_report::archive::SourceDbReference;
using bridge_report::archive::SourceDbValidationError;
using bridge_report::archive::decode_source_db_reference;
using bridge_report::archive::default_source_db_path;
using bridge_report::archive::encode_source_db_reference;
using bridge_report::archive::path_from_utf8;
using bridge_report::archive::validate_source_db;

constexpr std::string_view kSqliteHeader{"SQLite format 3\0", 16};

std::filesystem::path write_file(
    const std::filesystem::path& path, const std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return path;
}

std::filesystem::path scratch(const std::string& name) {
    // 名字里有中文时 path(std::string) 会走当前代码页并抛异常；夹具自己也得按 UTF-8 拼。
    return std::filesystem::temp_directory_path() / "bridge_report_source_db_tests" /
        path_from_utf8(name);
}

std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

}  // namespace

TEST(SourceDbReferenceTest, AcceptsARealSqliteFile) {
    const auto path = write_file(scratch("ok.sqlite"), std::string(kSqliteHeader) + "payload");

    const auto result = validate_source_db({utf8(path), "task-1"});

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.original_file_name, "ok.sqlite");
}

TEST(SourceDbReferenceTest, RejectsAFileThatIsNotASqliteDatabase) {
    // 用户很容易选错文件；错在这里报比让 Python 抛表结构错误清楚得多。
    const auto path = write_file(scratch("wrong.docx"), "PK\x03\x04 not a database");

    const auto result = validate_source_db({utf8(path), "task-1"});

    EXPECT_EQ(result.error, SourceDbValidationError::NotSqlite);
}

TEST(SourceDbReferenceTest, RejectsAMissingPath) {
    const auto result = validate_source_db({utf8(scratch("gone.sqlite")), "task-1"});

    EXPECT_EQ(result.error, SourceDbValidationError::PathMissing);
}

TEST(SourceDbReferenceTest, RejectsABlankTaskId) {
    const auto path = write_file(scratch("ok2.sqlite"), std::string(kSqliteHeader));

    EXPECT_EQ(validate_source_db({utf8(path), ""}).error,
              SourceDbValidationError::TaskIdMissing);
}

TEST(SourceDbReferenceTest, ReadsAPathWithChineseCharacters) {
    // Windows 当前代码页转换会在这类路径上失败；必须按 UTF-8 解释。
    const auto path = write_file(
        scratch("百股大桥离线库.sqlite"), std::string(kSqliteHeader));

    const auto result = validate_source_db({utf8(path), "task-1"});

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.original_file_name, "百股大桥离线库.sqlite");
}

TEST(SourceDbReferenceTest, SurvivesAnEncodeDecodeRoundTrip) {
    const SourceDbReference reference{"D:/数据/离线库.sqlite", "5e3f-task"};

    const auto decoded = decode_source_db_reference(encode_source_db_reference(reference));

    EXPECT_EQ(decoded.source_db_path, reference.source_db_path);
    EXPECT_EQ(decoded.task_id, reference.task_id);
}

TEST(SourceDbReferenceTest, RejectsAReferenceFileThatLostItsFields) {
    EXPECT_THROW(decode_source_db_reference("{\"task_id\":\"t\"}"), std::invalid_argument);
    EXPECT_THROW(decode_source_db_reference("not json"), std::invalid_argument);
}

TEST(SourceDbReferenceTest, KeepsNonAsciiPathsIntactThroughTheJson) {
    const auto encoded = encode_source_db_reference({"D:/数据/离线库.sqlite", "t"});

    // emitUTF8 关掉的话这里会变成 \uXXXX 转义，肉眼排查引用文件时很难认。
    EXPECT_NE(encoded.find("离线库"), std::string::npos);
}

TEST(SourceDbReferenceTest, PointsAtTheVendorsFixedLocationByDefault) {
    // 用户不会知道这串路径；界面默认就得替他填好。
    const auto path = default_source_db_path();

    ASSERT_FALSE(path.empty());
    const auto text = path.generic_u8string();
    const std::string utf8(text.begin(), text.end());
    EXPECT_NE(utf8.find("datacheck.hitek.com/databases/https_bridge.ilis.cn_0/1"),
              std::string::npos);
}
