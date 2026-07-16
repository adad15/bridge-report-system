#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/archive/TemporaryWordStorage.hpp"
#include "bridge_report/archive/WordInputArchive.hpp"

namespace {

std::filesystem::path test_root() {
    return std::filesystem::temp_directory_path() / "bridge-report-word-input-test";
}

}  // namespace

TEST(WordInputArchiveTest, AcceptsChineseNameAndUppercaseDocx) {
    const auto result = bridge_report::archive::validate_word_input(
        R"(C:\fakepath\绕阳河二号桥报告.DOCX)", "word-content", 1024);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.metadata.original_file_name, "绕阳河二号桥报告.DOCX");
    EXPECT_EQ(result.metadata.file_extension, ".docx");
    EXPECT_EQ(result.metadata.file_size_bytes, 12u);
    EXPECT_EQ(result.metadata.sha256.size(), 64u);
}

TEST(WordInputArchiveTest, AcceptsUnicodeReportNameWithoutFilesystemConversion) {
    const auto result = bridge_report::archive::validate_word_input(
        "Q202406002-JZ-506大桥定期检测报告—（2类）🚧.DOCX", "word-content", 1024);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.metadata.file_extension, ".docx");
}

TEST(WordInputArchiveTest, RejectsEmptyWrongExtensionAndOversize) {
    EXPECT_EQ(bridge_report::archive::validate_word_input("", "x", 10).error,
              bridge_report::archive::WordInputValidationError::InvalidFile);
    EXPECT_EQ(bridge_report::archive::validate_word_input("report.docx", "", 10).error,
              bridge_report::archive::WordInputValidationError::InvalidFile);
    EXPECT_EQ(bridge_report::archive::validate_word_input("report.doc", "x", 10).error,
              bridge_report::archive::WordInputValidationError::InvalidFile);
    EXPECT_EQ(bridge_report::archive::validate_word_input("report.docx", "123", 2).error,
              bridge_report::archive::WordInputValidationError::FileTooLarge);
}

TEST(WordInputArchiveTest, ArchivesAtomicallyUnderRootAndCanCompensate) {
    const auto root = test_root();
    std::filesystem::remove_all(root);
    const auto content = std::string("word-content");
    const auto validated = bridge_report::archive::validate_word_input("报告.docx", content, 1024);
    ASSERT_TRUE(validated.ok());
    const auto relative = std::filesystem::path("bridges") / "QL-1" / "报告.docx";

    const auto stored = bridge_report::archive::archive_word_input(
        root, relative, content, validated.metadata.sha256);

    ASSERT_TRUE(std::filesystem::is_regular_file(stored));
    std::ifstream input(stored, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), content);
    input.close();
    bridge_report::archive::remove_archived_word_input(root, relative);
    EXPECT_FALSE(std::filesystem::exists(stored));
    std::filesystem::remove_all(root);
}

TEST(TemporaryWordStorageTest, UsesFlatUuidNameAndDeletesIdempotently) {
    const auto root = test_root() / "temporary";
    std::filesystem::remove_all(root);
    const auto content = std::string("word-content");
    const auto validated = bridge_report::archive::validate_word_input(
        "Q202406002-JZ-506大桥定期检测报告—（2类）.docx", content, 1024);
    ASSERT_TRUE(validated.ok());
    const auto relative = bridge_report::archive::temporary_word_relative_path(
        "883a08d4-557d-4421-8cc3-c1036e19b56a");

    EXPECT_EQ(relative.generic_string(), "883a08d4-557d-4421-8cc3-c1036e19b56a.docx");
    const auto stored = bridge_report::archive::store_temporary_word(
        root, relative, content, validated.metadata.sha256);
    EXPECT_EQ(stored.parent_path(), std::filesystem::weakly_canonical(root));
    EXPECT_TRUE(std::filesystem::is_regular_file(stored));

    bridge_report::archive::remove_temporary_word(root, relative);
    EXPECT_FALSE(std::filesystem::exists(stored));
    EXPECT_NO_THROW(bridge_report::archive::remove_temporary_word(root, relative));
    EXPECT_THROW(
        bridge_report::archive::temporary_word_relative_path("not-a-uuid"),
        std::invalid_argument
    );
    std::filesystem::remove_all(root);
}
