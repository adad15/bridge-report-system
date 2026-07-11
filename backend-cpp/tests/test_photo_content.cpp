#include <filesystem>

#include <gtest/gtest.h>

#include "bridge_report/http/ReviewRoutes.hpp"

TEST(PhotoContentTest, ResolvesSafeArchivePathUnderRoot) {
    const auto root = std::filesystem::temp_directory_path() / "bridge-report-photo-content";
    const auto resolved = bridge_report::http::resolve_photo_content_path(root, "photos/photo_0001.jpg");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, std::filesystem::weakly_canonical(root / "photos/photo_0001.jpg"));
}

TEST(PhotoContentTest, RejectsArchivePathOutsideRoot) {
    EXPECT_FALSE(bridge_report::http::resolve_photo_content_path("D:/archive", "../secret.jpg").has_value());
    EXPECT_FALSE(bridge_report::http::resolve_photo_content_path("D:/archive", "C:/secret.jpg").has_value());
}
