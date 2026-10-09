#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/archive/StandardDocumentArchive.hpp"

namespace {

namespace fs = std::filesystem;
using namespace bridge_report::archive;

const std::string kPdf = "%PDF-1.4\n1 0 obj << >> endobj\n%%EOF\n";

class StandardDocumentArchiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
            ("standard-document-archive-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        fs::remove_all(root_);
        fs::create_directories(root_);
    }

    void TearDown() override { fs::remove_all(root_); }

    fs::path root_;
};

TEST_F(StandardDocumentArchiveTest, StoresThePdfUnderItsDocumentDirectory) {
    const auto archived = archive_standard_pdf(kPdf, "公路桥梁技术状况评定标准.pdf", root_, "doc-1");

    EXPECT_EQ(archived.original_file_name, "公路桥梁技术状况评定标准.pdf");
    EXPECT_EQ(archived.storage_relative_path.rfind("standards/doc-1/", 0), 0u);
    EXPECT_EQ(archived.storage_relative_path.substr(archived.storage_relative_path.size() - 4), ".pdf");
    EXPECT_EQ(archived.file_size_bytes, kPdf.size());
    EXPECT_EQ(archived.sha256.size(), 64u);

    std::ifstream input(root_ / archived.storage_relative_path, std::ios::binary);
    const std::string stored((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    EXPECT_EQ(stored, kPdf);
}

TEST_F(StandardDocumentArchiveTest, EveryUploadGetsItsOwnFile) {
    const auto first = archive_standard_pdf(kPdf, "a.pdf", root_, "doc-1");
    const auto second = archive_standard_pdf(kPdf, "a.pdf", root_, "doc-1");
    EXPECT_NE(first.storage_relative_path, second.storage_relative_path);
}

TEST_F(StandardDocumentArchiveTest, RejectsContentThatIsNotAPdf) {
    EXPECT_THROW(archive_standard_pdf("hello", "a.pdf", root_, "doc-1"), StandardDocumentNotPdfError);
    EXPECT_THROW(archive_standard_pdf(kPdf, "a.txt", root_, "doc-1"), StandardDocumentNotPdfError);
    EXPECT_THROW(archive_standard_pdf("", "a.pdf", root_, "doc-1"), StandardDocumentEmptyError);
    EXPECT_TRUE(fs::is_empty(root_));
}

TEST_F(StandardDocumentArchiveTest, AcceptsAnUppercaseExtension) {
    EXPECT_TRUE(looks_like_pdf(kPdf, "H21.PDF"));
}

TEST_F(StandardDocumentArchiveTest, RemovesOnlyWhenTheHashMatches) {
    const auto archived = archive_standard_pdf(kPdf, "a.pdf", root_, "doc-1");
    const auto absolute = root_ / archived.storage_relative_path;

    remove_archived_standard_pdf(root_, archived.storage_relative_path, std::string(64, '0'));
    EXPECT_TRUE(fs::exists(absolute));

    remove_archived_standard_pdf(root_, archived.storage_relative_path, archived.sha256);
    EXPECT_FALSE(fs::exists(absolute));
}

}  // namespace
