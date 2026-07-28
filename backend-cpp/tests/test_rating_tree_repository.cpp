#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/RatingTreeRepository.hpp"
#include "bridge_report/db/StandardRepository.hpp"
#include "bridge_report/rating_tree/RatingTreeCompiler.hpp"
#include "bridge_report/rating_tree/RatingTreePackageLoader.hpp"
#include "bridge_report/standards/StandardPackageLoader.hpp"

namespace {

class RatingTreeRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP();
        }
        client_ = bridge_report::db::create_db_client(
            bridge_report::config::PostgresConfig{}, 1);
        repository_ =
            std::make_unique<bridge_report::db::RatingTreeRepository>(client_);
    }

    void TearDown() override {
        if (client_) client_->closeAll();
    }

    bridge_report::rating_tree::EffectiveRatingTree compile_tree() {
        const auto root = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT);
        bridge_report::standards::StandardPackageLoader standard_loader;
        auto h21 = standard_loader.load(
            root / "standards/technical-condition/jtg-t-h21-2011/1.0.1");
        auto maintenance = standard_loader.load(
            root / "standards/maintenance/jtg-5120-2021/1.0.0");
        bridge_report::rating_tree::RatingTreePackageLoader extension_loader;
        auto extension = extension_loader.load(
            root / "standards/rating-tree/organization-bridge/1.0.0");
        EXPECT_TRUE(h21.ok());
        EXPECT_TRUE(maintenance.ok());
        EXPECT_TRUE(extension.ok());

        bridge_report::db::StandardRepository standards(client_);
        EXPECT_NE(
            standards.sync_package(h21.package->manifest).status,
            bridge_report::db::StandardPackageSyncStatus::ChecksumConflict);
        EXPECT_NE(
            standards.sync_package(maintenance.package->manifest).status,
            bridge_report::db::StandardPackageSyncStatus::ChecksumConflict);

        bridge_report::rating_tree::RatingTreeCompiler compiler;
        auto compiled = compiler.compile(
            *h21.package, &*maintenance.package, *extension.package);
        EXPECT_TRUE(compiled.ok());
        return std::move(*compiled.tree);
    }

    drogon::orm::DbClientPtr client_;
    std::unique_ptr<bridge_report::db::RatingTreeRepository> repository_;
};

TEST_F(RatingTreeRepositoryTest, SyncsPublishedTreeIdempotentlyAndRejectsConflict) {
    auto tree = compile_tree();
    const auto first = repository_->sync_published_tree(tree);
    EXPECT_TRUE(
        first.status == bridge_report::db::RatingTreeSyncStatus::Inserted ||
        first.status == bridge_report::db::RatingTreeSyncStatus::Unchanged);
    ASSERT_TRUE(first.rating_tree_version_id.has_value());

    const auto second = repository_->sync_published_tree(tree);
    EXPECT_EQ(second.status, bridge_report::db::RatingTreeSyncStatus::Unchanged);
    EXPECT_EQ(second.rating_tree_version_id, first.rating_tree_version_id);

    const auto stored =
        repository_->find_version_by_id(*first.rating_tree_version_id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->status, "published");
    EXPECT_EQ(stored->tree_content_checksum, tree.version.tree_content_checksum);

    tree.version.tree_content_checksum =
        "sha256:" + std::string(64, 'f');
    const auto conflict = repository_->sync_published_tree(tree);
    EXPECT_EQ(
        conflict.status,
        bridge_report::db::RatingTreeSyncStatus::ChecksumConflict);

    const auto listed = repository_->list_published_versions();
    EXPECT_TRUE(std::any_of(
        listed.begin(),
        listed.end(),
        [&](const auto& item) { return item.id == *first.rating_tree_version_id; }));
}

TEST_F(RatingTreeRepositoryTest, MissingSourcePackageDoesNotPublish) {
    auto tree = compile_tree();
    tree.version.tree_code += "-missing-source";
    tree.version.package_version += "-missing-source";
    tree.version.h21_standard_id = "missing-standard";
    tree.version.tree_content_checksum =
        "sha256:" + std::string(64, 'e');

    const auto outcome = repository_->sync_published_tree(tree);
    EXPECT_EQ(
        outcome.status,
        bridge_report::db::RatingTreeSyncStatus::SourcePackageNotFound);
    EXPECT_FALSE(outcome.rating_tree_version_id.has_value());
}

}  // namespace
