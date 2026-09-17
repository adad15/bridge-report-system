#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/archive/BridgeMediaArchive.hpp"

namespace {

namespace fs = std::filesystem;
using bridge_report::archive::archive_bridge_media;
using bridge_report::archive::BridgeMediaArchiveContext;
using bridge_report::archive::BridgeMediaArchiveError;
using bridge_report::archive::BridgeMediaTooLargeError;
using bridge_report::archive::remove_archived_bridge_media;

/// 一张 1×1 的 PNG，够让文件头识别认出来。
std::string tiny_png() {
    const unsigned char bytes[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xfc, 0xcf, 0xc0, 0x50,
        0x0f, 0x00, 0x04, 0x85, 0x01, 0x80, 0x84, 0xa9, 0x8c, 0x21, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    return std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

class BridgeMediaArchiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("bridge-media-archive-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        fs::remove_all(root_);
        fs::create_directories(root_);
        context_.archive_root = root_;
        context_.bridge_system_number = "QL-000014";
        context_.slot = "LOCATION_MAP";
    }

    void TearDown() override {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    fs::path root_;
    BridgeMediaArchiveContext context_;
};

TEST_F(BridgeMediaArchiveTest, WritesUnderAnAsciiPathKeyedBySystemNumberAndSlot) {
    const auto archived = archive_bridge_media(tiny_png(), "地理位置图.png", context_);

    // 路径里不放桥名：Windows 上 UTF-8 中文会被当本地代码页解释，写出乱码目录。
    EXPECT_EQ(archived.storage_relative_path.rfind("bridges/QL-000014/media/LOCATION_MAP/LOCATION_MAP-", 0), 0u);
    EXPECT_EQ(archived.file_extension, ".png");
    EXPECT_EQ(archived.sha256.size(), 64u);
    EXPECT_TRUE(fs::is_regular_file(root_ / archived.storage_relative_path));
}

TEST_F(BridgeMediaArchiveTest, RejectsContentThatIsNotAnImage) {
    EXPECT_THROW(archive_bridge_media("not an image", "fake.png", context_), BridgeMediaArchiveError);
}

TEST_F(BridgeMediaArchiveTest, RejectsContentOverTheLimit) {
    context_.max_bytes = 10;
    EXPECT_THROW(archive_bridge_media(tiny_png(), "big.png", context_), BridgeMediaTooLargeError);
}

// 删之前要先读内容核对哈希。读完不关就删，Windows 上会因为句柄还开着静默删不掉。
TEST_F(BridgeMediaArchiveTest, RemovesTheFileWhenTheHashMatches) {
    const auto archived = archive_bridge_media(tiny_png(), "map.png", context_);
    const auto absolute = root_ / archived.storage_relative_path;
    ASSERT_TRUE(fs::is_regular_file(absolute));

    remove_archived_bridge_media(root_, archived.storage_relative_path, archived.sha256);

    EXPECT_FALSE(fs::exists(absolute));
}

TEST_F(BridgeMediaArchiveTest, KeepsTheFileWhenTheHashDoesNotMatch) {
    const auto archived = archive_bridge_media(tiny_png(), "map.png", context_);
    const auto absolute = root_ / archived.storage_relative_path;

    remove_archived_bridge_media(root_, archived.storage_relative_path, std::string(64, '0'));

    EXPECT_TRUE(fs::is_regular_file(absolute));
}

}  // namespace
