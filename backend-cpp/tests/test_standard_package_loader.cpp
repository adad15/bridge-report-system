#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/standards/StandardPackageLoader.hpp"

namespace {

using bridge_report::standards::StandardPackageLoader;

std::filesystem::path fixture_root() {
    return std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
           "backend-cpp/tests/fixtures/standards";
}

class TemporaryPackage {
public:
    explicit TemporaryPackage(const std::string& fixture_name) {
        static std::atomic<unsigned long long> sequence{0};
        path_ = std::filesystem::temp_directory_path() /
                ("bridge-report-standard-package-" +
                 std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
        std::filesystem::copy(
            fixture_root() / fixture_name,
            path_,
            std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::overwrite_existing);
    }

    ~TemporaryPackage() { std::filesystem::remove_all(path_); }

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
        throw std::runtime_error("test fixture JSON could not be parsed");
    }
    return value;
}

void write_json(const std::filesystem::path& path, const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << Json::writeString(builder, value) << '\n';
}

void write_text(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value;
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

bool has_issue(const bridge_report::standards::StandardLoadResult& result, const std::string& code) {
    for (const auto& issue : result.issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

TEST(StandardPackageLoaderTest, LoadsTechnicalAndMaintenancePackages) {
    StandardPackageLoader loader;
    TemporaryPackage technical("valid-technical");
    TemporaryPackage maintenance("valid-maintenance");

    const auto technical_result = loader.load(technical.path());
    const auto maintenance_result = loader.load(maintenance.path());

    ASSERT_TRUE(technical_result.ok());
    ASSERT_TRUE(maintenance_result.ok());
    EXPECT_EQ(technical_result.package->manifest.standard_id, "TEST-TECH-2026");
    EXPECT_EQ(maintenance_result.package->manifest.standard_id, "TEST-MAINT-2026");
    EXPECT_EQ(technical_result.package->definitions.size(), 2u);
    EXPECT_EQ(maintenance_result.package->definitions.size(), 1u);
}

TEST(StandardPackageLoaderTest, DiscoversManifestDirectoriesRecursively) {
    StandardPackageLoader loader;

    const auto packages = loader.discover(fixture_root());

    ASSERT_EQ(packages.size(), 3u);
    EXPECT_EQ(packages[0].filename(), "invalid-checksum");
    EXPECT_EQ(packages[1].filename(), "valid-maintenance");
    EXPECT_EQ(packages[2].filename(), "valid-technical");
}

TEST(StandardPackageLoaderTest, RejectsMissingManifest) {
    StandardPackageLoader loader;
    TemporaryPackage package("valid-technical");
    std::filesystem::remove(package.path() / "manifest.json");

    const auto result = loader.load(package.path());

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(has_issue(result, "manifest_missing"));
}

TEST(StandardPackageLoaderTest, RejectsMissingRequiredManifestFields) {
    const std::string required_fields[] = {
        "standard_family", "standard_id", "package_version", "contract_version", "algorithm_id", "status"};
    StandardPackageLoader loader;

    for (const auto& field : required_fields) {
        SCOPED_TRACE(field);
        TemporaryPackage package("valid-technical");
        auto manifest = read_json(package.path() / "manifest.json");
        manifest.removeMember(field);
        write_json(package.path() / "manifest.json", manifest);

        const auto result = loader.load(package.path());

        EXPECT_FALSE(result.ok());
        EXPECT_TRUE(has_issue(result, "manifest_field_missing"));
    }
}

TEST(StandardPackageLoaderTest, RejectsUnknownContractVersion) {
    StandardPackageLoader loader;
    TemporaryPackage package("valid-technical");
    auto manifest = read_json(package.path() / "manifest.json");
    manifest["contract_version"] = 99;
    write_json(package.path() / "manifest.json", manifest);

    const auto result = loader.load(package.path());

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(has_issue(result, "contract_version_unsupported"));
}

TEST(StandardPackageLoaderTest, RejectsUnknownPackageStatus) {
    StandardPackageLoader loader;
    TemporaryPackage package("valid-technical");
    auto manifest = read_json(package.path() / "manifest.json");
    manifest["status"] = "draft";
    write_json(package.path() / "manifest.json", manifest);

    const auto result = loader.load(package.path());

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(has_issue(result, "package_status_unsupported"));
}

TEST(StandardPackageLoaderTest, RejectsChecksumMismatch) {
    StandardPackageLoader loader;
    TemporaryPackage package("invalid-checksum");

    const auto result = loader.load(package.path());

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(has_issue(result, "content_checksum_mismatch"));
}

TEST(StandardPackageLoaderTest, ChecksumIgnoresEntryOrderLineEndingsAndObjectKeyOrder) {
    StandardPackageLoader loader;
    TemporaryPackage first("valid-technical");
    TemporaryPackage second("valid-technical");

    auto first_manifest = read_json(first.path() / "manifest.json");
    first_manifest["entry_files"] = Json::Value(Json::arrayValue);
    first_manifest["entry_files"].append("rules.json");
    first_manifest["entry_files"].append("more.json");
    write_json(first.path() / "manifest.json", first_manifest);
    write_text(first.path() / "rules.json",
               "{\r\n  \"definitions\": [{\"references\": [], \"id\": \"rule.one\"}]\r\n}\r\n");
    write_text(first.path() / "more.json",
               "{\r\n  \"definitions\": [{\"id\": \"rule.two\", \"references\": [\"rule.one\"]}]\r\n}\r\n");

    auto second_manifest = read_json(second.path() / "manifest.json");
    second_manifest["entry_files"] = Json::Value(Json::arrayValue);
    second_manifest["entry_files"].append("more.json");
    second_manifest["entry_files"].append("rules.json");
    write_json(second.path() / "manifest.json", second_manifest);
    write_text(second.path() / "rules.json",
               "{\"definitions\":[{\"id\":\"rule.one\",\"references\":[]}]}\n");
    write_text(second.path() / "more.json",
               "{\"definitions\":[{\"references\":[\"rule.one\"],\"id\":\"rule.two\"}]}\n");

    const auto first_checksum = loader.calculate_checksum(first.path());
    const auto second_checksum = loader.calculate_checksum(second.path());

    ASSERT_TRUE(first_checksum.ok());
    ASSERT_TRUE(second_checksum.ok());
    EXPECT_EQ(first_checksum.checksum, second_checksum.checksum);
}

TEST(StandardPackageLoaderTest, RejectsDuplicateAndDanglingDefinitionReferences) {
    StandardPackageLoader loader;

    TemporaryPackage duplicate("valid-technical");
    auto duplicate_rules = read_json(duplicate.path() / "rules.json");
    duplicate_rules["definitions"].append(duplicate_rules["definitions"][0]);
    write_json(duplicate.path() / "rules.json", duplicate_rules);
    seal_package(duplicate.path());
    const auto duplicate_result = loader.load(duplicate.path());
    EXPECT_TRUE(has_issue(duplicate_result, "definition_id_duplicate"));

    TemporaryPackage dangling("valid-technical");
    auto dangling_rules = read_json(dangling.path() / "rules.json");
    dangling_rules["definitions"][1]["references"][0] = "missing.rule";
    write_json(dangling.path() / "rules.json", dangling_rules);
    seal_package(dangling.path());
    const auto dangling_result = loader.load(dangling.path());
    EXPECT_TRUE(has_issue(dangling_result, "definition_reference_dangling"));
}

TEST(StandardPackageLoaderTest, RejectsCrossPackageReferences) {
    StandardPackageLoader loader;
    TemporaryPackage package("valid-technical");
    auto rules = read_json(package.path() / "rules.json");
    rules["definitions"][1]["references"][0] = "OTHER-STANDARD::component.deck";
    write_json(package.path() / "rules.json", rules);
    seal_package(package.path());

    const auto result = loader.load(package.path());

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(has_issue(result, "definition_reference_cross_package"));
}

TEST(StandardPackageLoaderTest, ErrorsDoNotExposeAbsolutePackagePath) {
    StandardPackageLoader loader;
    TemporaryPackage package("invalid-checksum");

    const auto result = loader.load(package.path());

    ASSERT_FALSE(result.issues.empty());
    for (const auto& issue : result.issues) {
        EXPECT_EQ(issue.message.find(package.path().string()), std::string::npos);
    }
}

}  // namespace
