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
            root / "standards/technical-condition/jtg-t-h21-2011/1.0.2");
        auto maintenance = standard_loader.load(
            root / "standards/maintenance/jtg-5120-2021/1.0.0");
        bridge_report::rating_tree::RatingTreePackageLoader extension_loader;
        auto extension = extension_loader.load(
            root / "standards/rating-tree/organization-bridge/1.0.1");
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

    // 1.0.3 是带受控规则包的版本，锁定的是修正后的 H21 1.0.3。
    bridge_report::rating_tree::EffectiveRatingTree compile_rule_pack_tree() {
        const auto root = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT);
        bridge_report::standards::StandardPackageLoader standard_loader;
        auto h21 = standard_loader.load(
            root / "standards/technical-condition/jtg-t-h21-2011/1.0.3");
        auto maintenance = standard_loader.load(
            root / "standards/maintenance/jtg-5120-2021/1.0.0");
        bridge_report::rating_tree::RatingTreePackageLoader extension_loader;
        auto extension = extension_loader.load(
            root / "standards/rating-tree/organization-bridge/1.0.3");
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

    bridge_report::rating_tree::EffectiveRatingTree compile_current_tree() {
        const auto root = std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT);
        bridge_report::standards::StandardPackageLoader standard_loader;
        auto h21 = standard_loader.load(
            root / "standards/technical-condition/jtg-t-h21-2011/1.0.4");
        auto maintenance = standard_loader.load(
            root / "standards/maintenance/jtg-5120-2021/1.0.0");
        auto extension = bridge_report::rating_tree::RatingTreePackageLoader().load(
            root / "standards/rating-tree/organization-bridge/2.0.3");
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

        auto compiled = bridge_report::rating_tree::RatingTreeCompiler().compile(
            *h21.package, &*maintenance.package, *extension.package);
        EXPECT_TRUE(compiled.ok());
        return std::move(*compiled.tree);
    }

    drogon::orm::DbClientPtr client_;
    std::unique_ptr<bridge_report::db::RatingTreeRepository> repository_;
};

TEST_F(RatingTreeRepositoryTest, PublishesAndReloadsTheControlledMatchingRulePack) {
    const auto tree = compile_rule_pack_tree();
    ASSERT_FALSE(tree.keyword_rules.empty());

    const auto sync = repository_->sync_published_tree(tree);
    ASSERT_TRUE(
        sync.status == bridge_report::db::RatingTreeSyncStatus::Inserted ||
        sync.status == bridge_report::db::RatingTreeSyncStatus::Unchanged);
    ASSERT_TRUE(sync.rating_tree_version_id.has_value());

    const auto loaded =
        repository_->load_published_tree(*sync.rating_tree_version_id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->keyword_rules.size(), tree.keyword_rules.size());
    EXPECT_EQ(loaded->aliases.size(), tree.aliases.size());
    // 规则顺序稳定，且目标节点是本版本内的真实节点。
    for (std::size_t index = 1; index < loaded->keyword_rules.size(); ++index) {
        const auto& previous = loaded->keyword_rules[index - 1];
        const auto& current = loaded->keyword_rules[index];
        EXPECT_TRUE(
            previous.sort_order < current.sort_order ||
            (previous.sort_order == current.sort_order &&
             previous.rule_id <= current.rule_id));
    }
    bool has_auto_water_rule = false;
    for (const auto& rule : loaded->keyword_rules) {
        EXPECT_TRUE(loaded->nodes.contains(rule.target_node_id));
        EXPECT_FALSE(rule.positive_keywords.empty());
        if (rule.auto_bind && rule.positive_keywords.front() == "渗水") {
            has_auto_water_rule = true;
            EXPECT_EQ(loaded->nodes.at(rule.target_node_id).display_name, "水损");
            EXPECT_FALSE(rule.excluded_keywords.empty());
        }
    }
    EXPECT_TRUE(has_auto_water_rule);
}

TEST_F(RatingTreeRepositoryTest, PersistsTheCurrentSourceTree) {
    const auto tree = compile_current_tree();

    const auto sync = repository_->sync_published_tree(tree);

    ASSERT_TRUE(
        sync.status == bridge_report::db::RatingTreeSyncStatus::Inserted ||
        sync.status == bridge_report::db::RatingTreeSyncStatus::Unchanged);
    ASSERT_TRUE(sync.rating_tree_version_id.has_value());
    const auto loaded =
        repository_->load_published_tree(*sync.rating_tree_version_id);
    ASSERT_TRUE(loaded.has_value());
    const auto found = std::find_if(
        loaded->nodes.begin(),
        loaded->nodes.end(),
        [](const auto& item) {
            return item.second.display_number ==
                std::optional<std::string>("9.1.1-10") &&
                item.second.display_name == "水损害";
        });
    ASSERT_NE(found, loaded->nodes.end());
    const auto& water = found->second;
    EXPECT_TRUE(water.is_scoring);
    EXPECT_TRUE(water.uses_source_scale_descriptions);
    EXPECT_EQ(water.scale_descriptions.at(3), "渗水、水蚀严重；范围＜30%");
    EXPECT_EQ(water.deduction_points.at(4), 50);
    EXPECT_EQ(water.sort_order, 100);

    const auto crack = std::find_if(
        loaded->nodes.begin(),
        loaded->nodes.end(),
        [](const auto& item) {
            return item.second.display_number ==
                std::optional<std::string>("9.1.2-1") &&
                item.second.display_name == "裂缝";
        });
    ASSERT_NE(crack, loaded->nodes.end());
    ASSERT_EQ(crack->second.source_mappings.size(), 1u);
    EXPECT_EQ(crack->second.source_mappings[0].source_group_number, "9.1.2");
    EXPECT_EQ(
        crack->second.source_mappings[0].source_indicator_number,
        "9.1.2-1");
}

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
    const auto loaded =
        repository_->load_published_tree(*first.rating_tree_version_id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->version.tree_content_checksum, tree.version.tree_content_checksum);
    EXPECT_EQ(loaded->nodes.size(), tree.nodes.size());
    EXPECT_EQ(loaded->aliases.size(), tree.aliases.size());

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

TEST_F(RatingTreeRepositoryTest, BackfillsTheOnlyPublishedTreeForAnUnboundProfile) {
    auto tree = compile_tree();
    const auto tree_sync = repository_->sync_published_tree(tree);
    ASSERT_TRUE(tree_sync.rating_tree_version_id.has_value());

    const auto package_ids = client_->execSqlSync(
        "select technical_condition_package_id::text as technical_id,"
        "maintenance_package_id::text as maintenance_id "
        "from rating_tree_versions where id=$1::uuid",
        *tree_sync.rating_tree_version_id);
    ASSERT_EQ(package_ids.size(), 1u);

    const auto users = client_->execSqlSync(
        "insert into users(username,display_name,password_hash,role) "
        "values('rating-tree-backfill-' || gen_random_uuid()::text,"
        "'评定树回填测试','test','admin') returning id::text as id");
    ASSERT_EQ(users.size(), 1u);
    const auto user_id = users[0]["id"].as<std::string>();

    client_->execSqlSync(
        "select set_config("
        "'bridge_report.allow_unbound_rating_tree_profile','on',false)");
    const auto profiles = client_->execSqlSync(
        "insert into project_standard_profiles("
        "technical_condition_package_id,maintenance_package_id,"
        "created_by_user_id,change_reason) "
        "values($1::uuid,$2::uuid,$3::uuid,'唯一评定树回填测试') "
        "returning id::text as id",
        package_ids[0]["technical_id"].as<std::string>(),
        package_ids[0]["maintenance_id"].as<std::string>(),
        user_id);
    client_->execSqlSync(
        "select set_config("
        "'bridge_report.allow_unbound_rating_tree_profile','off',false)");
    ASSERT_EQ(profiles.size(), 1u);
    const auto profile_id = profiles[0]["id"].as<std::string>();

    const auto backfill = repository_->backfill_unique_profile_versions();
    EXPECT_GE(backfill.updated_count, 1u);
    const auto bound = client_->execSqlSync(
        "select rating_tree_version_id::text as tree_id "
        "from project_standard_profiles where id=$1::uuid",
        profile_id);
    ASSERT_EQ(bound.size(), 1u);
    EXPECT_EQ(
        bound[0]["tree_id"].as<std::string>(),
        *tree_sync.rating_tree_version_id);

    client_->execSqlSync(
        "delete from project_standard_profiles where id=$1::uuid",
        profile_id);
    client_->execSqlSync("delete from users where id=$1::uuid", user_id);
}

}  // namespace
