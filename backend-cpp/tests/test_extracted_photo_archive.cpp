#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/archive/ExtractedPhotoArchive.hpp"

namespace {

using bridge_report::archive::ArchivedPhotoBatch;
using bridge_report::archive::PhotoArchiveContext;
using bridge_report::archive::PhotoArchiveError;
using bridge_report::archive::PhotoTooLargeError;
using bridge_report::archive::UploadedPhotoInput;
using bridge_report::archive::archive_extracted_photos;
using bridge_report::archive::archive_uploaded_photo;
using bridge_report::archive::cleanup_archived_photo_batch;
using bridge_report::archive::remove_archived_photo;

class TempDirectory {
public:
    TempDirectory() {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        root_ = std::filesystem::temp_directory_path() / ("bridge-report-photo-archive-" + suffix);
        std::filesystem::create_directories(staging());
        std::filesystem::create_directories(archive());
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    std::filesystem::path staging() const { return root_ / "staging"; }
    std::filesystem::path archive() const { return root_ / "archive"; }

private:
    std::filesystem::path root_;
};

void write_bytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<unsigned char> jpeg_fixture() {
    return {0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0xff, 0xd9};
}

Json::Value python_response_with_photo(const std::string& candidate_id, const std::string& file_name) {
    Json::Value data;
    data["photos"] = Json::Value(Json::arrayValue);
    Json::Value photo;
    photo["candidate_id"] = candidate_id;
    photo["photo_number"] = "2.1-1";
    photo["extracted_file"]["temporary_file_name"] = file_name;
    photo["extracted_file"]["original_caption"] = "主梁裂缝";
    photo["extracted_file"]["archive_relative_path"] = Json::Value(Json::nullValue);
    data["photos"].append(photo);
    return data;
}

PhotoArchiveContext make_context(const TempDirectory& temp) {
    return PhotoArchiveContext{
        temp.staging(), temp.archive(), "QL-000001", "绕阳河二号桥", 2026, "DRJL-000001", "软件Word导入"
    };
}

TEST(ExtractedPhotoArchiveTest, CopiesCandidatePhotoAndRewritesJson) {
    TempDirectory temp;
    write_bytes(temp.staging() / "photo_0001.jpeg", jpeg_fixture());

    const auto batch = archive_extracted_photos(
        python_response_with_photo("photo_0001", "photo_0001.jpeg"), make_context(temp));

    ASSERT_EQ(batch.files.size(), 1u);
    const auto& file = batch.files[0];
    EXPECT_TRUE(std::filesystem::exists(temp.archive() / file.storage_relative_path));
    EXPECT_EQ(file.candidate_id, "photo_0001");
    EXPECT_EQ(file.file_extension, ".jpeg");
    EXPECT_EQ(file.file_size_bytes, jpeg_fixture().size());
    EXPECT_EQ(file.sha256.size(), 64u);
    EXPECT_EQ(batch.data["photos"][0]["extracted_file"]["archive_relative_path"].asString(),
              file.storage_relative_path.generic_string());
}

TEST(ExtractedPhotoArchiveTest, RejectsPathTraversalAbsoluteAndMissingFiles) {
    TempDirectory temp;
    EXPECT_THROW(archive_extracted_photos(
        python_response_with_photo("photo_0001", "../outside.jpg"), make_context(temp)), PhotoArchiveError);
    EXPECT_THROW(archive_extracted_photos(
        python_response_with_photo("photo_0001", "C:/outside.jpg"), make_context(temp)), PhotoArchiveError);
    EXPECT_THROW(archive_extracted_photos(
        python_response_with_photo("photo_0001", "missing.jpg"), make_context(temp)), PhotoArchiveError);
}

TEST(ExtractedPhotoArchiveTest, RejectsFileWhoseMagicDoesNotMatchExtension) {
    TempDirectory temp;
    write_bytes(temp.staging() / "fake.jpg", {'n', 'o', 't', '-', 'j', 'p', 'e', 'g'});

    EXPECT_THROW(archive_extracted_photos(
        python_response_with_photo("photo_0001", "fake.jpg"), make_context(temp)), PhotoArchiveError);
}

TEST(ExtractedPhotoArchiveTest, RollsBackFilesCopiedBeforeLaterCandidateFails) {
    TempDirectory temp;
    write_bytes(temp.staging() / "photo_0001.jpg", jpeg_fixture());
    auto data = python_response_with_photo("photo_0001", "photo_0001.jpg");
    data["photos"].append(python_response_with_photo("photo_0002", "missing.jpg")["photos"][0]);

    EXPECT_THROW(archive_extracted_photos(data, make_context(temp)), PhotoArchiveError);
    EXPECT_TRUE(std::filesystem::is_empty(temp.archive()));
}

TEST(ExtractedPhotoArchiveTest, CleanupRemovesOnlyFilesListedInBatch) {
    TempDirectory temp;
    write_bytes(temp.staging() / "photo_0001.jpg", jpeg_fixture());
    const auto batch = archive_extracted_photos(
        python_response_with_photo("photo_0001", "photo_0001.jpg"), make_context(temp));
    const auto unrelated = temp.archive() / "keep.txt";
    write_bytes(unrelated, {'k'});

    cleanup_archived_photo_batch(temp.archive(), batch);

    EXPECT_FALSE(std::filesystem::exists(temp.archive() / batch.files[0].storage_relative_path));
    EXPECT_TRUE(std::filesystem::exists(unrelated));
}

TEST(ExtractedPhotoArchiveTest, FailedRetryDoesNotDeletePreexistingArchivedPhoto) {
    TempDirectory temp;
    write_bytes(temp.staging() / "photo_0001.jpg", jpeg_fixture());
    const auto first = archive_extracted_photos(
        python_response_with_photo("photo_0001", "photo_0001.jpg"), make_context(temp));
    const auto archived_path = temp.archive() / first.files[0].storage_relative_path;
    ASSERT_TRUE(std::filesystem::exists(archived_path));

    auto retry_data = python_response_with_photo("photo_0001", "photo_0001.jpg");
    retry_data["photos"].append(python_response_with_photo("photo_0002", "missing.jpg")["photos"][0]);

    EXPECT_THROW(archive_extracted_photos(retry_data, make_context(temp)), PhotoArchiveError);
    EXPECT_TRUE(std::filesystem::exists(archived_path));
}

TEST(ExtractedPhotoArchiveTest, RejectsCorruptPreexistingArchiveAtExpectedPath) {
    TempDirectory temp;
    write_bytes(temp.staging() / "photo_0001.jpg", jpeg_fixture());
    const auto first = archive_extracted_photos(
        python_response_with_photo("photo_0001", "photo_0001.jpg"), make_context(temp));
    const auto archived_path = temp.archive() / first.files[0].storage_relative_path;
    write_bytes(archived_path, {'b', 'a', 'd'});

    EXPECT_THROW(
        archive_extracted_photos(
            python_response_with_photo("photo_0001", "photo_0001.jpg"), make_context(temp)),
        PhotoArchiveError
    );
    EXPECT_TRUE(std::filesystem::exists(archived_path));
}

std::string jpeg_bytes() {
    const auto bytes = jpeg_fixture();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

UploadedPhotoInput upload_input(std::string content, std::string file_name) {
    return UploadedPhotoInput{std::move(content), std::move(file_name), "manual_photo_0001", 0};
}

TEST(UploadedPhotoArchiveTest, ArchivesAnUploadedImageUnderTheImportPhotoDirectory) {
    TempDirectory temp;

    const auto file = archive_uploaded_photo(upload_input(jpeg_bytes(), "IMG_2031.JPG"), make_context(temp));

    const auto path = temp.archive() / file.storage_relative_path;
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(std::filesystem::file_size(path), jpeg_bytes().size());
    EXPECT_EQ(file.candidate_id, "manual_photo_0001");
    EXPECT_EQ(file.original_file_name, "IMG_2031.JPG");
    EXPECT_EQ(file.file_extension, ".jpg");
    EXPECT_EQ(file.sha256.size(), 64u);
    EXPECT_TRUE(file.created_by_batch);
    // 与 Word 抽出的照片同目录，删除导入时一并带走。
    EXPECT_EQ(file.storage_relative_path.parent_path().filename().string(), "photos");
}

TEST(UploadedPhotoArchiveTest, RejectsAFakeImageWhoseExtensionLiesAboutItsContent) {
    TempDirectory temp;

    EXPECT_THROW(archive_uploaded_photo(upload_input("not-an-image", "evil.jpg"), make_context(temp)),
                 PhotoArchiveError);
    // 内容是真图但扩展名对不上，同样拒绝。
    EXPECT_THROW(archive_uploaded_photo(upload_input(jpeg_bytes(), "photo.png"), make_context(temp)),
                 PhotoArchiveError);
    EXPECT_TRUE(std::filesystem::is_empty(temp.archive()));
}

TEST(UploadedPhotoArchiveTest, RejectsContentOverTheConfiguredLimit) {
    TempDirectory temp;
    auto input = upload_input(jpeg_bytes(), "photo.jpg");
    input.max_bytes = jpeg_bytes().size() - 1;

    EXPECT_THROW(archive_uploaded_photo(input, make_context(temp)), PhotoTooLargeError);
    EXPECT_TRUE(std::filesystem::is_empty(temp.archive()));
}

TEST(UploadedPhotoArchiveTest, RefusesToLetTheCandidateIdEscapeTheArchiveRoot) {
    TempDirectory temp;
    auto input = upload_input(jpeg_bytes(), "photo.jpg");
    input.candidate_id = "../../escape";

    const auto file = archive_uploaded_photo(input, make_context(temp));

    // 编号被清洗成安全片段，文件仍落在归档根之内。
    const auto resolved = std::filesystem::weakly_canonical(temp.archive() / file.storage_relative_path);
    const auto root = std::filesystem::weakly_canonical(temp.archive());
    EXPECT_EQ(resolved.string().rfind(root.string(), 0), 0u);
}

TEST(UploadedPhotoArchiveTest, LeavesNoTemporaryFileBehindWhenValidationFails) {
    TempDirectory temp;

    EXPECT_THROW(archive_uploaded_photo(upload_input("", "photo.jpg"), make_context(temp)), PhotoArchiveError);

    EXPECT_TRUE(std::filesystem::is_empty(temp.archive()));
}

TEST(UploadedPhotoArchiveTest, RemovesAnArchivedPhotoOnlyWhenTheHashStillMatches) {
    TempDirectory temp;
    const auto file = archive_uploaded_photo(upload_input(jpeg_bytes(), "photo.jpg"), make_context(temp));
    const auto path = temp.archive() / file.storage_relative_path;

    remove_archived_photo(temp.archive(), file.storage_relative_path, "0000");
    EXPECT_TRUE(std::filesystem::exists(path));

    remove_archived_photo(temp.archive(), file.storage_relative_path, file.sha256);
    EXPECT_FALSE(std::filesystem::exists(path));
}

}  // namespace
