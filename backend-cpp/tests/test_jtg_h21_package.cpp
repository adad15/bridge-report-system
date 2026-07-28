#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

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

std::filesystem::path h21_package_root_v101() {
    return std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
           "standards/technical-condition/jtg-t-h21-2011/1.0.1";
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

Json::Value read_json(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Json::CharReaderBuilder builder;
    Json::Value value;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &value, &errors)) {
        throw std::runtime_error("test package JSON could not be parsed");
    }
    return value;
}

void write_json(const std::filesystem::path& path, const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << Json::writeString(builder, value) << '\n';
}

void seal_package(const std::filesystem::path& package_path) {
    StandardPackageLoader loader;
    const auto checksum = loader.calculate_checksum(package_path);
    if (!checksum.ok()) {
        throw std::runtime_error("test package checksum could not be calculated");
    }
    auto manifest = read_json(package_path / "manifest.json");
    manifest["content_checksum"] = *checksum.checksum;
    write_json(package_path / "manifest.json", manifest);
}

bool has_issue(
    const bridge_report::standards::StandardLoadResult& result,
    const std::string& code) {
    for (const auto& issue : result.issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

TEST(JtgH21PackageTest, ChecksumCanBeCalculated) {
    StandardPackageLoader loader;
    const auto checksum = loader.calculate_checksum(h21_package_root());
    ASSERT_TRUE(checksum.ok());
    EXPECT_EQ(
        *checksum.checksum,
        "sha256:842f4e5a702d0ae6533be2aa9bea61e7d823c31d866b64a34dbc9a9347fdd898");
}

TEST(JtgH21PackageTest, Version101AddsOfficialMajorComponentClassification) {
    StandardPackageLoader loader;
    const auto checksum = loader.calculate_checksum(h21_package_root_v101());
    ASSERT_TRUE(checksum.ok());
    EXPECT_EQ(
        *checksum.checksum,
        "sha256:0602d4b4084ba35251876c414fd736eb08bf8e3f47fb729e89e72247cc837ba8");

    const auto loaded = loader.load(h21_package_root_v101());
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.package->manifest.package_version, "1.0.1");
    std::size_t bridge_type_count = 0;
    for (const auto& [id, definition] : loaded.package->definitions) {
        if (!id.starts_with("h21.bridge_type.")) {
            continue;
        }
        ++bridge_type_count;
        ASSERT_TRUE(definition.payload["major_component_ids"].isArray()) << id;
        ASSERT_FALSE(definition.payload["major_component_ids"].empty()) << id;
        for (const auto& component_id : definition.payload["major_component_ids"]) {
            EXPECT_TRUE(loaded.package->definitions.contains(component_id.asString())) << id;
        }
    }
    EXPECT_EQ(bridge_type_count, 6u);
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

TEST(JtgH21PackageTest, Version101EveryDefectScaleHasOfficialDescription) {
    StandardPackageLoader loader;
    const auto result = loader.load(h21_package_root_v101());
    ASSERT_TRUE(result.ok());

    const auto& catalogs = document(*result.package, "defect-indicators.json")["definitions"];
    std::size_t indicator_count = 0;
    for (const auto& catalog : catalogs) {
        for (const auto& indicator : catalog["indicators"]) {
            ++indicator_count;
            ASSERT_TRUE(indicator["scale_descriptions"].isObject()) << indicator["id"].asString();
            const auto members = indicator["scale_descriptions"].getMemberNames();
            ASSERT_EQ(members.size(), indicator["allowed_scales"].size())
                << indicator["id"].asString();
            for (const auto& scale : indicator["allowed_scales"]) {
                const auto key = std::to_string(scale.asInt());
                ASSERT_TRUE(indicator["scale_descriptions"].isMember(key))
                    << indicator["id"].asString() << " scale " << key;
                ASSERT_TRUE(indicator["scale_descriptions"][key].isString());
                EXPECT_FALSE(indicator["scale_descriptions"][key].asString().empty());
            }
        }
    }
    EXPECT_EQ(indicator_count, 234u);
}

TEST(JtgH21PackageTest, Version101RejectsIncompleteOrExtraScaleDescriptions) {
    const std::vector<std::pair<std::string, Json::Value>> mutations = {
        {"missing", Json::Value()},
        {"empty", Json::Value("")},
        {"extra", Json::Value("不应存在的额外标度")},
    };

    for (const auto& [kind, value] : mutations) {
        SCOPED_TRACE(kind);
        TemporaryPackageCopy package(h21_package_root_v101());
        auto document = read_json(package.path() / "defect-indicators.json");
        auto& indicator = document["definitions"][0]["indicators"][0];
        if (kind == "missing") {
            indicator["scale_descriptions"].removeMember("1");
        } else if (kind == "empty") {
            indicator["scale_descriptions"]["1"] = value;
        } else {
            indicator["scale_descriptions"]["4"] = value;
        }
        write_json(package.path() / "defect-indicators.json", document);
        seal_package(package.path());

        const auto result = StandardPackageLoader().load(package.path());
        EXPECT_FALSE(result.ok());
        EXPECT_TRUE(has_issue(result, "defect_scale_descriptions_invalid"));
    }
}

TEST(JtgH21PackageTest, Version101UsesTheOfficialThreeScaleDamperTable) {
    StandardPackageLoader loader;
    const auto result = loader.load(h21_package_root_v101());
    ASSERT_TRUE(result.ok());

    const auto& catalogs = document(*result.package, "defect-indicators.json")["definitions"];
    const Json::Value* damper = nullptr;
    for (const auto& catalog : catalogs) {
        for (const auto& indicator : catalog["indicators"]) {
            if (indicator["id"].asString() == "h21.defect.8_6_1_1") {
                damper = &indicator;
            }
        }
    }
    ASSERT_NE(damper, nullptr);
    EXPECT_EQ((*damper)["allowed_scales"].size(), 3u);
    EXPECT_EQ((*damper)["deduction_rule_id"].asString(), "h21.deduction.scale_table.max_3");
    EXPECT_EQ((*damper)["scale_descriptions"]["3"].asString(),
              "减震装置出现较多处损坏，部分功能失效");
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
