#include <filesystem>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/standards/StandardRegistry.hpp"
#include "bridge_report/standards/H21Evaluator.hpp"
#include "bridge_report/standards/StandardPackageLoader.hpp"

namespace {

using bridge_report::standards::StandardAlgorithmAdapter;
using bridge_report::standards::StandardFamily;
using bridge_report::standards::StandardPackage;
using bridge_report::standards::StandardPackageKey;
using bridge_report::standards::StandardRegistry;

StandardPackage make_package(
    StandardFamily family,
    std::string standard_id,
    std::string package_version,
    std::string checksum,
    std::string algorithm_id = "test-algorithm") {
    StandardPackage package;
    package.manifest.family = family;
    package.manifest.standard_id = std::move(standard_id);
    package.manifest.package_version = std::move(package_version);
    package.manifest.content_checksum = std::move(checksum);
    package.manifest.algorithm_id = std::move(algorithm_id);
    return package;
}

class FakeAlgorithm final : public StandardAlgorithmAdapter {
public:
    explicit FakeAlgorithm(std::string id) : id_(std::move(id)) {}
    std::string algorithm_id() const override { return id_; }

private:
    std::string id_;
};

TEST(StandardRegistryTest, GetsPackageByFamilyIdentityAndVersion) {
    StandardRegistry registry;
    auto result = registry.register_package(make_package(
        StandardFamily::technical_condition, "TEST-TECH", "1.0.0", "sha256:one"));

    const auto* package = registry.find(
        {StandardFamily::technical_condition, "TEST-TECH", "1.0.0"});

    ASSERT_TRUE(result.accepted);
    ASSERT_NE(package, nullptr);
    EXPECT_EQ(package->manifest.content_checksum, "sha256:one");
    EXPECT_EQ(registry.find({StandardFamily::maintenance, "TEST-TECH", "1.0.0"}), nullptr);
}

TEST(StandardRegistryTest, AllowsMultiplePackageVersions) {
    StandardRegistry registry;

    EXPECT_TRUE(registry.register_package(make_package(
        StandardFamily::technical_condition, "TEST-TECH", "1.0.0", "sha256:one")).accepted);
    EXPECT_TRUE(registry.register_package(make_package(
        StandardFamily::technical_condition, "TEST-TECH", "2.0.0", "sha256:two")).accepted);

    EXPECT_EQ(registry.package_count(), 2u);
    EXPECT_NE(registry.find({StandardFamily::technical_condition, "TEST-TECH", "1.0.0"}), nullptr);
    EXPECT_NE(registry.find({StandardFamily::technical_condition, "TEST-TECH", "2.0.0"}), nullptr);
}

TEST(StandardRegistryTest, RejectsSameIdentityAndVersionWithDifferentDigest) {
    StandardRegistry registry;
    registry.register_package(make_package(
        StandardFamily::technical_condition, "TEST-TECH", "1.0.0", "sha256:one"));

    const auto result = registry.register_package(make_package(
        StandardFamily::technical_condition, "TEST-TECH", "1.0.0", "sha256:different"));

    EXPECT_FALSE(result.accepted);
    ASSERT_TRUE(result.issue.has_value());
    EXPECT_EQ(result.issue->code, "package_identity_checksum_conflict");
    EXPECT_EQ(registry.package_count(), 1u);
}

TEST(StandardRegistryTest, ReRegisteringExactPackageIsIdempotent) {
    StandardRegistry registry;
    registry.register_package(make_package(
        StandardFamily::maintenance, "TEST-MAINT", "1.0.0", "sha256:one"));

    const auto result = registry.register_package(make_package(
        StandardFamily::maintenance, "TEST-MAINT", "1.0.0", "sha256:one"));

    EXPECT_TRUE(result.accepted);
    EXPECT_FALSE(result.inserted);
    EXPECT_EQ(registry.package_count(), 1u);
}

TEST(StandardRegistryTest, RegistersAndCreatesNonH21AlgorithmAdapter) {
    StandardRegistry registry;
    EXPECT_TRUE(registry.register_algorithm(
        "future-standard-v3",
        [](const StandardPackage&) {
            return std::make_unique<FakeAlgorithm>("future-standard-v3");
        }));
    EXPECT_FALSE(registry.register_algorithm(
        "future-standard-v3",
        [](const StandardPackage&) {
            return std::make_unique<FakeAlgorithm>("duplicate");
        }));
    registry.register_package(make_package(
        StandardFamily::technical_condition,
        "FUTURE-STANDARD",
        "3.0.0",
        "sha256:future",
        "future-standard-v3"));

    const auto adapter = registry.create_algorithm(
        {StandardFamily::technical_condition, "FUTURE-STANDARD", "3.0.0"});

    ASSERT_NE(adapter, nullptr);
    EXPECT_EQ(adapter->algorithm_id(), "future-standard-v3");
}

TEST(StandardRegistryTest, CreatesBuiltInH21TechnicalConditionEvaluator) {
    bridge_report::standards::StandardPackageLoader loader;
    const auto root = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
        "standards/technical-condition/jtg-t-h21-2011/1.0.2";
    auto loaded = loader.load(root);
    ASSERT_TRUE(loaded.ok());
    const auto key = loaded.package->key();
    StandardRegistry registry;
    ASSERT_TRUE(registry.register_package(std::move(*loaded.package)).accepted);

    const auto algorithm = registry.create_algorithm(key);

    ASSERT_NE(algorithm, nullptr);
    EXPECT_NE(dynamic_cast<bridge_report::standards::H21Evaluator*>(algorithm.get()), nullptr);
    EXPECT_EQ(algorithm->algorithm_id(), "jtg-h21-2011");
}

}  // namespace
