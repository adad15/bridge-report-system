#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/standards/StandardPackageLoader.hpp"

namespace {

using bridge_report::standards::StandardFamily;
using bridge_report::standards::StandardPackageLoader;

std::filesystem::path jtg5120_package_root() {
    return std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
           "standards/maintenance/jtg-5120-2021/1.0.0";
}

class TemporaryPackageCopy {
public:
    explicit TemporaryPackageCopy(const std::filesystem::path& source) {
        static std::atomic<unsigned long long> sequence{0};
        path_ = std::filesystem::temp_directory_path() /
                ("bridge-report-jtg5120-package-" +
                 std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::filesystem::remove_all(path_);
        std::filesystem::copy(
            source,
            path_,
            std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::overwrite_existing);
    }

    ~TemporaryPackageCopy() { std::filesystem::remove_all(path_); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

bool contains_forbidden_scoring_key(const Json::Value& value) {
    if (value.isObject()) {
        const auto members = value.getMemberNames();
        for (const auto& member : members) {
            if (member == "deduction_rule_id" || member == "deduction_rules" ||
                member == "score_formula" || member == "weight_set") {
                return true;
            }
            if (contains_forbidden_scoring_key(value[member])) {
                return true;
            }
        }
    } else if (value.isArray()) {
        for (const auto& item : value) {
            if (contains_forbidden_scoring_key(item)) {
                return true;
            }
        }
    }
    return false;
}

TEST(Jtg5120PackageTest, ChecksumCanBeCalculated) {
    StandardPackageLoader loader;
    const auto checksum = loader.calculate_checksum(jtg5120_package_root());
    ASSERT_TRUE(checksum.ok());
}

TEST(Jtg5120PackageTest, DigestChangesWhenAnyRuleContentChanges) {
    StandardPackageLoader loader;
    TemporaryPackageCopy package(jtg5120_package_root());
    const auto before = loader.calculate_checksum(package.path());
    ASSERT_TRUE(before.ok());

    const auto source_path = package.path() / "sources.json";
    std::ifstream input(source_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto marker = content.find("official_standard_pdf");
    ASSERT_NE(marker, std::string::npos);
    content.replace(marker, std::string("official_standard_pdf").size(), "official_standard_pdf_modified");
    std::ofstream output(source_path, std::ios::binary | std::ios::trunc);
    output << content;
    output.close();

    const auto after = loader.calculate_checksum(package.path());
    ASSERT_TRUE(after.ok());
    EXPECT_NE(before.checksum, after.checksum);
}

TEST(Jtg5120PackageTest, LoadsOfficialIdentityAndFirstPhaseMaintenanceRules) {
    StandardPackageLoader loader;
    const auto result = loader.load(jtg5120_package_root());

    ASSERT_TRUE(result.ok());
    const auto& package = *result.package;
    EXPECT_EQ(package.manifest.family, StandardFamily::maintenance);
    EXPECT_EQ(package.manifest.standard_id, "JTG_5120_2021");
    EXPECT_EQ(package.manifest.standard_code, "JTG 5120—2021");
    EXPECT_EQ(package.manifest.package_version, "1.0.0");
    EXPECT_EQ(package.manifest.effective_date, "2021-11-01");

    const auto& levels = package.definitions.at("jtg5120.maintenance_level.catalog").payload["levels"];
    const auto& types = package.definitions.at("jtg5120.inspection_type.catalog").payload["types"];
    const auto& intervals =
        package.definitions.at("jtg5120.periodic_inspection.interval").payload["maximum_intervals"];
    const auto& content =
        package.definitions.at("jtg5120.periodic_inspection.content").payload["content_groups"];
    EXPECT_EQ(levels.size(), 3u);
    EXPECT_EQ(types.size(), 5u);
    EXPECT_EQ(intervals.size(), 3u);
    EXPECT_EQ(content.size(), 11u);
}

TEST(Jtg5120PackageTest, ContainsNoH21ScoringFormulaOrDeductionTable) {
    StandardPackageLoader loader;
    const auto result = loader.load(jtg5120_package_root());
    ASSERT_TRUE(result.ok());

    for (const auto& [id, definition] : result.package->definitions) {
        EXPECT_FALSE(id.starts_with("h21."));
        EXPECT_FALSE(contains_forbidden_scoring_key(definition.payload)) << id;
    }
    for (const auto& [name, value] : result.package->documents) {
        EXPECT_FALSE(contains_forbidden_scoring_key(value)) << name;
    }
}

}  // namespace
