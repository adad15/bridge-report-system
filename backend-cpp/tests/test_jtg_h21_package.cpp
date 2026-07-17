#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/standards/StandardPackageLoader.hpp"

namespace {

using bridge_report::standards::StandardFamily;
using bridge_report::standards::StandardPackage;
using bridge_report::standards::StandardPackageLoader;

std::filesystem::path h21_package_root() {
    return std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
           "standards/technical-condition/jtg-t-h21-2011/1.0.0";
}

const Json::Value& document(const StandardPackage& package, const std::string& name) {
    const auto found = package.documents.find(name);
    if (found == package.documents.end()) {
        throw std::runtime_error("expected H21 package document is missing");
    }
    return found->second;
}

class TemporaryPackageCopy {
public:
    explicit TemporaryPackageCopy(const std::filesystem::path& source) {
        static std::atomic<unsigned long long> sequence{0};
        path_ = std::filesystem::temp_directory_path() /
                ("bridge-report-h21-package-" +
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

TEST(JtgH21PackageTest, ChecksumCanBeCalculated) {
    StandardPackageLoader loader;
    const auto checksum = loader.calculate_checksum(h21_package_root());
    ASSERT_TRUE(checksum.ok());
}

TEST(JtgH21PackageTest, DigestChangesWhenAnyRuleContentChanges) {
    StandardPackageLoader loader;
    TemporaryPackageCopy package(h21_package_root());
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

TEST(JtgH21PackageTest, LoadsOfficialIdentityAndAllBridgeTypes) {
    StandardPackageLoader loader;
    const auto result = loader.load(h21_package_root());

    ASSERT_TRUE(result.ok());
    const auto& package = *result.package;
    EXPECT_EQ(package.manifest.family, StandardFamily::technical_condition);
    EXPECT_EQ(package.manifest.standard_id, "JTG_T_H21_2011");
    EXPECT_EQ(package.manifest.standard_code, "JTG/T H21—2011");
    EXPECT_EQ(package.manifest.package_version, "1.0.0");
    EXPECT_EQ(package.manifest.effective_date, "2011-09-01");

    std::size_t bridge_type_count = 0;
    for (const auto& [id, definition] : package.definitions) {
        if (id.starts_with("h21.bridge_type.")) {
            ++bridge_type_count;
            EXPECT_TRUE(definition.payload.isMember("source_clause"));
            EXPECT_FALSE(definition.references.empty());
        }
    }
    EXPECT_EQ(bridge_type_count, 6u);
}

TEST(JtgH21PackageTest, EveryWeightSetSumsToOne) {
    StandardPackageLoader loader;
    const auto result = loader.load(h21_package_root());
    ASSERT_TRUE(result.ok());

    std::size_t weight_set_count = 0;
    for (const auto& [id, definition] : result.package->definitions) {
        if (!id.starts_with("h21.weight_set.")) {
            continue;
        }
        ++weight_set_count;
        ASSERT_TRUE(definition.payload["weights"].isArray()) << id;
        double total = 0.0;
        for (const auto& weight : definition.payload["weights"]) {
            total += weight["value"].asDouble();
        }
        EXPECT_NEAR(total, 1.0, 1e-9) << id;
    }
    EXPECT_EQ(weight_set_count, 10u);
}

TEST(JtgH21PackageTest, EveryComponentDeclaresBridgeTypesHierarchyAndGenerationPolicy) {
    StandardPackageLoader loader;
    const auto result = loader.load(h21_package_root());
    ASSERT_TRUE(result.ok());

    std::size_t component_count = 0;
    for (const auto& [id, definition] : result.package->definitions) {
        if (!id.starts_with("h21.component.")) {
            continue;
        }
        ++component_count;
        ASSERT_TRUE(definition.payload["bridge_type_ids"].isArray()) << id;
        ASSERT_FALSE(definition.payload["bridge_type_ids"].empty()) << id;
        for (const auto& bridge_type_id : definition.payload["bridge_type_ids"]) {
            EXPECT_TRUE(result.package->definitions.contains(bridge_type_id.asString())) << id;
        }
        EXPECT_TRUE(definition.payload["structure_part"].isString()) << id;
        EXPECT_TRUE(definition.payload["generatable"].isBool()) << id;
    }
    EXPECT_EQ(component_count, 40u);
}

TEST(JtgH21PackageTest, EveryDefectHasStableScaleDeductionAndSource) {
    StandardPackageLoader loader;
    const auto result = loader.load(h21_package_root());
    ASSERT_TRUE(result.ok());

    const auto& catalogs = document(*result.package, "defect-indicators.json")["definitions"];
    ASSERT_TRUE(catalogs.isArray());
    std::set<std::string> ids;
    std::size_t indicator_count = 0;
    for (const auto& catalog : catalogs) {
        ASSERT_TRUE(catalog["applicable_component_ids"].isArray());
        ASSERT_FALSE(catalog["applicable_component_ids"].empty());
        ASSERT_TRUE(catalog["indicators"].isArray());
        for (const auto& indicator : catalog["indicators"]) {
            ++indicator_count;
            ASSERT_TRUE(indicator["id"].isString());
            EXPECT_TRUE(ids.insert(indicator["id"].asString()).second);
            ASSERT_TRUE(indicator["allowed_scales"].isArray());
            ASSERT_FALSE(indicator["allowed_scales"].empty());
            for (Json::ArrayIndex index = 0; index < indicator["allowed_scales"].size(); ++index) {
                EXPECT_EQ(indicator["allowed_scales"][index].asInt(), static_cast<int>(index + 1));
            }
            ASSERT_TRUE(indicator["deduction_rule_id"].isString());
            EXPECT_TRUE(result.package->definitions.contains(indicator["deduction_rule_id"].asString()));
            EXPECT_TRUE(indicator["source_table"].isString());
            EXPECT_FALSE(indicator["source_table"].asString().empty());
        }
    }
    EXPECT_EQ(indicator_count, 234u);
}

TEST(JtgH21PackageTest, AllProfilesReferenceBridgeAndScoringLevels) {
    StandardPackageLoader loader;
    const auto result = loader.load(h21_package_root());
    ASSERT_TRUE(result.ok());

    std::size_t profile_count = 0;
    for (const auto& [id, definition] : result.package->definitions) {
        if (!id.starts_with("h21.weight_profile.")) {
            continue;
        }
        ++profile_count;
        EXPECT_GE(definition.references.size(), 5u) << id;
        EXPECT_TRUE(result.package->definitions.contains(definition.payload["bridge_type_id"].asString()));
    }
    EXPECT_EQ(profile_count, 6u);
    EXPECT_TRUE(result.package->definitions.contains("h21.calculation.component_score"));
    EXPECT_TRUE(result.package->definitions.contains("h21.calculation.component_category_score"));
    EXPECT_TRUE(result.package->definitions.contains("h21.calculation.structure_part_score"));
    EXPECT_TRUE(result.package->definitions.contains("h21.calculation.overall_score"));
}

}  // namespace
