#include <filesystem>
#include <memory>
#include <type_traits>

#include <gtest/gtest.h>

#include "bridge_report/standards/Jtg5120MaintenanceStandard.hpp"
#include "bridge_report/standards/StandardPackageLoader.hpp"
#include "bridge_report/standards/TechnicalConditionStandard.hpp"

namespace {

using bridge_report::standards::InspectionTypesResult;
using bridge_report::standards::Jtg5120MaintenanceStandard;
using bridge_report::standards::MaintenanceLevelsResult;
using bridge_report::standards::MaintenanceQueryContext;
using bridge_report::standards::MaintenanceStandard;
using bridge_report::standards::PeriodicInspectionRequirementResult;
using bridge_report::standards::ProjectRequirementValidation;
using bridge_report::standards::StandardFamily;
using bridge_report::standards::StandardPackage;
using bridge_report::standards::StandardPackageLoader;
using bridge_report::standards::StandardRegistry;

std::filesystem::path package_root() {
    return std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
           "standards/maintenance/jtg-5120-2021/1.0.0";
}

StandardPackage load_package() {
    StandardPackageLoader loader;
    auto loaded = loader.load(package_root());
    if (!loaded.ok()) {
        throw std::runtime_error("unable to load JTG 5120 package");
    }
    return std::move(*loaded.package);
}

TEST(Jtg5120MaintenanceStandardTest, ReadsThreeMaintenanceLevelsWithSources) {
    Jtg5120MaintenanceStandard standard(load_package());

    const auto result = standard.maintenance_levels();

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.value->size(), 3u);
    EXPECT_EQ(result.value->front().id, "jtg5120.maintenance_level.i");
    EXPECT_EQ(result.value->front().code, "I");
    EXPECT_EQ(result.value->front().name, "Ⅰ级养护");
    EXPECT_EQ(result.value->front().source.rule_id,
              "jtg5120.maintenance_level.catalog");
    EXPECT_EQ(result.value->front().source.source_reference, "3.1.1");
}

TEST(Jtg5120MaintenanceStandardTest, ReadsFiveInspectionTypesWithOwnClauses) {
    Jtg5120MaintenanceStandard standard(load_package());

    const auto result = standard.inspection_types();

    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.value->size(), 5u);
    EXPECT_EQ((*result.value)[0].code, "initial");
    EXPECT_EQ((*result.value)[1].code, "daily_patrol");
    EXPECT_EQ((*result.value)[2].code, "routine");
    EXPECT_EQ((*result.value)[3].code, "periodic");
    EXPECT_EQ((*result.value)[3].source.source_reference, "3.5");
    EXPECT_EQ((*result.value)[4].code, "special");
}

TEST(Jtg5120MaintenanceStandardTest, QueriesPeriodicIntervalAndContentByLevel) {
    Jtg5120MaintenanceStandard standard(load_package());

    const auto level_i = standard.periodic_inspection_requirements({
        "jtg5120.maintenance_level.i",
        "jtg5120.inspection_type.periodic",
        std::nullopt,
    });
    const auto level_ii = standard.periodic_inspection_requirements({
        "jtg5120.maintenance_level.ii",
        "jtg5120.inspection_type.periodic",
        std::nullopt,
    });

    ASSERT_TRUE(level_i.ok());
    ASSERT_TRUE(level_ii.ok());
    EXPECT_DOUBLE_EQ(level_i.value->maximum_interval_years, 1.0);
    EXPECT_DOUBLE_EQ(level_ii.value->maximum_interval_years, 3.0);
    ASSERT_EQ(level_i.value->content_groups.size(), 11u);
    EXPECT_EQ(level_i.value->content_groups.front().id,
              "jtg5120.periodic_content.records");
    ASSERT_EQ(level_i.value->sources.size(), 12u);
    EXPECT_EQ(level_i.value->sources.front().source_reference, "3.5.1");
    EXPECT_EQ(level_i.value->sources.back().source_reference, "3.5.12");
}

TEST(Jtg5120MaintenanceStandardTest, RejectsMissingOrUnsupportedContext) {
    Jtg5120MaintenanceStandard standard(load_package());

    const auto missing = standard.periodic_inspection_requirements({});
    const auto unknown_level = standard.periodic_inspection_requirements({
        "jtg5120.maintenance_level.unknown",
        "jtg5120.inspection_type.periodic",
        std::nullopt,
    });
    const auto unsupported_type = standard.periodic_inspection_requirements({
        "jtg5120.maintenance_level.i",
        "jtg5120.inspection_type.routine",
        std::nullopt,
    });

    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.issues.front().code, "maintenance_level_required");
    ASSERT_FALSE(unknown_level.ok());
    EXPECT_EQ(unknown_level.issues.front().code, "maintenance_level_unsupported");
    ASSERT_FALSE(unsupported_type.ok());
    EXPECT_EQ(unsupported_type.issues.front().code,
              "inspection_type_unsupported_for_periodic_query");
}

TEST(Jtg5120MaintenanceStandardTest, ValidatesPlannedPeriodicInterval) {
    Jtg5120MaintenanceStandard standard(load_package());

    const auto valid = standard.validate_project_requirements({
        "jtg5120.maintenance_level.i",
        "jtg5120.inspection_type.periodic",
        1.0,
    });
    const auto too_long = standard.validate_project_requirements({
        "jtg5120.maintenance_level.i",
        "jtg5120.inspection_type.periodic",
        1.5,
    });
    const auto missing_interval = standard.validate_project_requirements({
        "jtg5120.maintenance_level.i",
        "jtg5120.inspection_type.periodic",
        std::nullopt,
    });

    EXPECT_TRUE(valid.valid);
    EXPECT_FALSE(too_long.valid);
    ASSERT_EQ(too_long.issues.size(), 1u);
    EXPECT_EQ(too_long.issues.front().code, "periodic_interval_exceeds_maximum");
    EXPECT_FALSE(missing_interval.valid);
    EXPECT_EQ(missing_interval.issues.front().code, "planned_interval_required");
}

TEST(Jtg5120MaintenanceStandardTest, MissingRulesReturnIssuesInsteadOfDefaults) {
    auto package = load_package();
    package.definitions.erase("jtg5120.periodic_inspection.interval");
    Jtg5120MaintenanceStandard standard(std::move(package));

    const auto result = standard.periodic_inspection_requirements({
        "jtg5120.maintenance_level.i",
        "jtg5120.inspection_type.periodic",
        std::nullopt,
    });

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "maintenance_rule_missing");
}

TEST(Jtg5120MaintenanceStandardTest, RegistryCreatesBuiltInAdapterWithoutScoringApi) {
    static_assert(std::is_base_of_v<MaintenanceStandard, Jtg5120MaintenanceStandard>);
    static_assert(!std::is_base_of_v<
                  bridge_report::standards::TechnicalConditionStandard,
                  Jtg5120MaintenanceStandard>);
    auto package = load_package();
    const auto key = package.key();
    StandardRegistry registry;
    ASSERT_TRUE(registry.register_package(std::move(package)).accepted);

    const auto algorithm = registry.create_algorithm(key);

    ASSERT_NE(algorithm, nullptr);
    EXPECT_NE(dynamic_cast<MaintenanceStandard*>(algorithm.get()), nullptr);
    EXPECT_EQ(algorithm->algorithm_id(), "jtg-5120-2021-maintenance-query");
}

class FutureMaintenanceStandard final : public MaintenanceStandard {
public:
    explicit FutureMaintenanceStandard(StandardPackage package) : package_(std::move(package)) {}
    std::string algorithm_id() const override { return package_.manifest.algorithm_id; }
    const bridge_report::standards::StandardManifest& metadata() const noexcept override {
        return package_.manifest;
    }
    MaintenanceLevelsResult maintenance_levels() const override {
        return {std::vector<bridge_report::standards::MaintenanceLevel>{}, {}};
    }
    InspectionTypesResult inspection_types() const override {
        return {std::vector<bridge_report::standards::InspectionType>{}, {}};
    }
    PeriodicInspectionRequirementResult periodic_inspection_requirements(
        const MaintenanceQueryContext&) const override {
        return {{}, {{"not_supported", "测试规范未实现。", {}, {}}}};
    }
    ProjectRequirementValidation validate_project_requirements(
        const MaintenanceQueryContext&) const override {
        return {false, {{"not_supported", "测试规范未实现。", {}, {}}}, {}};
    }

private:
    StandardPackage package_;
};

TEST(Jtg5120MaintenanceStandardTest, FutureMaintenanceAdapterUsesSameRegistryContract) {
    StandardRegistry registry;
    ASSERT_TRUE(registry.register_algorithm(
        "future-maintenance-v2",
        [](const StandardPackage& package) {
            return std::make_unique<FutureMaintenanceStandard>(package);
        }));
    StandardPackage package;
    package.manifest.family = StandardFamily::maintenance;
    package.manifest.standard_id = "FUTURE_MAINTENANCE";
    package.manifest.package_version = "2.0.0";
    package.manifest.content_checksum = "sha256:future";
    package.manifest.algorithm_id = "future-maintenance-v2";
    const auto key = package.key();
    ASSERT_TRUE(registry.register_package(std::move(package)).accepted);

    const auto algorithm = registry.create_algorithm(key);

    ASSERT_NE(algorithm, nullptr);
    EXPECT_NE(dynamic_cast<MaintenanceStandard*>(algorithm.get()), nullptr);
    EXPECT_EQ(algorithm->algorithm_id(), "future-maintenance-v2");
}

}  // namespace
