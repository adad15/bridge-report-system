#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/standards/StandardPackageLoader.hpp"
#include "support/jtg5230_fixtures.hpp"

namespace {

using bridge_report::standards::StandardPackageLoader;
namespace jtg5230 = bridge_report::tests::jtg5230;

Json::Value read_manifest() {
    std::ifstream input(jtg5230::package_root() / "manifest.json", std::ios::binary);
    Json::CharReaderBuilder builder;
    Json::Value manifest;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &manifest, &errors)) {
        ADD_FAILURE() << "manifest.json 无法解析：" << errors;
    }
    return manifest;
}

// 改了包内容先跑这一条：失败信息里的实际值就是新摘要，写回 manifest.json。
TEST(Jtg5230PackageTest, ManifestChecksumMatchesContent) {
    const auto checksum = StandardPackageLoader().calculate_checksum(jtg5230::package_root());

    ASSERT_TRUE(checksum.ok());
    EXPECT_EQ(read_manifest()["content_checksum"].asString(), *checksum.checksum);
}

TEST(Jtg5230PackageTest, LoadsAsDraftPackage) {
    const auto result = StandardPackageLoader().load(jtg5230::package_root());

    ASSERT_TRUE(result.ok());
    const auto& manifest = result.package->manifest;
    EXPECT_EQ(manifest.standard_id, "JTG_T_5230_2026");
    EXPECT_EQ(manifest.standard_code, "JTG/T 5230—2026");
    EXPECT_EQ(manifest.algorithm_id, "jtg-t-5230-2026");
    EXPECT_EQ(manifest.status, "draft");
    EXPECT_EQ(manifest.effective_date, "2026-11-01");
    EXPECT_EQ(manifest.entry_files.size(), 8u);
}

TEST(Jtg5230PackageTest, RepositoryRuntimeScanSkipsIt) {
    const auto result = StandardPackageLoader().load_runtime_packages(
        std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) / "standards");

    EXPECT_TRUE(result.issues.empty());
    EXPECT_TRUE(std::any_of(
        result.skipped_drafts.begin(), result.skipped_drafts.end(), [](const auto& manifest) {
            return manifest.standard_id == "JTG_T_5230_2026" &&
                   manifest.package_version == "1.0.0";
        }));
    EXPECT_TRUE(std::none_of(
        result.packages.begin(), result.packages.end(), [](const auto& package) {
            return package.manifest.standard_id == "JTG_T_5230_2026";
        }));
    EXPECT_TRUE(std::any_of(
        result.packages.begin(), result.packages.end(), [](const auto& package) {
            return package.manifest.standard_id == "JTG_T_H21_2011" &&
                   package.manifest.package_version == "1.0.4";
        }));
}

}  // namespace
