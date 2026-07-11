#include <filesystem>

#include <gtest/gtest.h>

#include "bridge_report/archive/ArchivePaths.hpp"

TEST(ArchivePathsTest, SanitizesUnsafePathParts) {
    EXPECT_EQ(
        bridge_report::archive::sanitize_path_part("DRJL-000001_软件/Word:导入*?"),
        "DRJL-000001_软件_Word_导入__"
    );
}

TEST(ArchivePathsTest, BuildsImportInputRelativePath) {
    const auto path = bridge_report::archive::build_import_input_relative_path(
        "QL-000001",
        "绕阳河二号桥",
        2026,
        "DRJL-000001",
        "软件Word导入",
        "GDWJ-000001",
        "绕阳河二号桥报告.docx"
    );

    EXPECT_EQ(
        path.generic_string(),
        "bridges/QL-000001_绕阳河二号桥/2026/imports/DRJL-000001_软件Word导入/input/GDWJ-000001_绕阳河二号桥报告.docx"
    );
}

TEST(ArchivePathsTest, BuildsExtractedPhotoRelativePath) {
    const auto path = bridge_report::archive::build_import_photo_relative_path(
        "QL-000001",
        "绕阳河二号桥",
        2026,
        "DRJL-000001",
        "软件Word导入",
        "GDWJ-000002",
        "照片2.1-1.jpg"
    );

    EXPECT_EQ(
        path.generic_string(),
        "bridges/QL-000001_绕阳河二号桥/2026/imports/DRJL-000001_软件Word导入/photos/GDWJ-000002_照片2.1-1.jpg"
    );
}

TEST(ArchivePathsTest, DetectsOnlySafeRelativeArchivePaths) {
    EXPECT_TRUE(bridge_report::archive::is_safe_archive_relative_path(
        std::filesystem::path("bridges/QL-000001/2026/imports/file.docx")
    ));
    EXPECT_FALSE(bridge_report::archive::is_safe_archive_relative_path(
        std::filesystem::path("D:/BridgeReportArchive/file.docx")
    ));
    EXPECT_FALSE(bridge_report::archive::is_safe_archive_relative_path(
        std::filesystem::path("/absolute/file.docx")
    ));
    EXPECT_FALSE(bridge_report::archive::is_safe_archive_relative_path(
        std::filesystem::path("../outside/file.docx")
    ));
    EXPECT_FALSE(bridge_report::archive::is_safe_archive_relative_path(std::filesystem::path(".")));
}

TEST(ArchivePathsTest, ResolvesSafePathUnderConfiguredRoot) {
    const auto root = std::filesystem::temp_directory_path() / "bridge-report-safe-root";

    const auto resolved = bridge_report::archive::resolve_path_under_root(root, "photos/photo.jpg");

    EXPECT_EQ(resolved, std::filesystem::weakly_canonical(root / "photos/photo.jpg"));
}

TEST(ArchivePathsTest, RejectsResolvedPathOutsideConfiguredRoot) {
    const auto root = std::filesystem::temp_directory_path() / "bridge-report-safe-root";

    EXPECT_THROW(
        bridge_report::archive::resolve_path_under_root(root, "../outside.jpg"),
        std::invalid_argument
    );
}
