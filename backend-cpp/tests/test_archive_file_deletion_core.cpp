#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "bridge_report/deletion/ArchiveFileDeletionCore.hpp"

namespace {

std::filesystem::path make_root(const char* name) {
    auto root = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "inside");
    return root;
}

}  // namespace

TEST(ArchiveFileDeletionCoreTest, DeletesSafeFileAndTreatsMissingAsSuccess) {
    const auto root = make_root("bridge_report_cleanup_core_success");
    const auto file = root / "inside" / "report.docx";
    std::ofstream(file) << "test";
    bridge_report::deletion::ArchiveFileDeletionCore core(root);

    EXPECT_TRUE(core.remove("inside/report.docx"));
    EXPECT_FALSE(std::filesystem::exists(file));
    EXPECT_FALSE(core.remove("inside/report.docx"));

    std::filesystem::remove_all(root);
}

TEST(ArchiveFileDeletionCoreTest, RejectsPathOutsideArchiveRoot) {
    const auto root = make_root("bridge_report_cleanup_core_unsafe");
    bridge_report::deletion::ArchiveFileDeletionCore core(root);

    EXPECT_THROW(core.remove("../outside.docx"), std::invalid_argument);

    std::filesystem::remove_all(root);
}
