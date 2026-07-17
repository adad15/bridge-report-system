#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <drogon/orm/Exception.h>
#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/StandardRepository.hpp"

namespace {

using bridge_report::db::CreateStandardProfileRequest;
using bridge_report::db::CreateStandardProfileStatus;
using bridge_report::db::SetStandardPackageEnabledStatus;
using bridge_report::db::StandardPackageSyncStatus;
using bridge_report::standards::StandardFamily;
using bridge_report::standards::StandardManifest;

class StandardRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP();
        }
        client_ = bridge_report::db::create_db_client(
            bridge_report::config::PostgresConfig{}, 1);
        repository_ = std::make_unique<bridge_report::db::StandardRepository>(client_);
        suffix_ = client_->execSqlSync(
            "select replace(gen_random_uuid()::text, '-', '') as value")[0]["value"].as<std::string>();
        const auto user = client_->execSqlSync(
            "insert into users (username, display_name, password_hash, role) "
            "values ($1, '规范仓储测试管理员', 'not-a-real-hash', 'admin') returning id::text as id",
            "standard_repo_" + suffix_);
        user_id_ = user[0]["id"].as<std::string>();
    }

    void TearDown() override {
        if (!client_) {
            return;
        }
        try {
            client_->execSqlSync(
                "delete from inspection_years where bridge_id in "
                "(select id from bridges where bridge_name=$1)",
                bridge_name());
            client_->execSqlSync("delete from bridges where bridge_name=$1", bridge_name());
            client_->execSqlSync(
                "delete from project_standard_profiles where created_by_user_id=$1::uuid",
                user_id_);
            client_->execSqlSync(
                "delete from standard_packages where standard_id like $1",
                "TEST-" + suffix_ + "%");
            client_->execSqlSync("delete from users where id=$1::uuid", user_id_);
        } catch (...) {
        }
        client_->closeAll();
    }

    StandardManifest manifest(StandardFamily family, const std::string& name, const char digest) const {
        StandardManifest value;
        value.family = family;
        value.standard_id = "TEST-" + suffix_ + "-" + name;
        value.standard_code = "TEST " + name;
        value.standard_name = "测试规范 " + name;
        value.official_edition = "2026";
        value.package_version = "1.0.0";
        value.contract_version = 1;
        value.algorithm_id = "test-" + name;
        value.effective_date = "2026-01-01";
        value.content_checksum = "sha256:" + std::string(64, digest);
        value.status = "active";
        return value;
    }

    std::string bridge_name() const { return "规范仓储测试桥-" + suffix_; }

    std::pair<std::string, std::string> sync_pair() {
        const auto technical = repository_->sync_package(
            manifest(StandardFamily::technical_condition, "TECH", '1'));
        const auto maintenance = repository_->sync_package(
            manifest(StandardFamily::maintenance, "MAINT", '2'));
        return {*technical.package_id, *maintenance.package_id};
    }

    CreateStandardProfileRequest profile_request(
        const std::string& technical_id,
        const std::string& maintenance_id,
        const std::string& reason = "仓储测试组合") const {
        return {technical_id, maintenance_id, user_id_, reason};
    }

    drogon::orm::DbClientPtr client_;
    std::unique_ptr<bridge_report::db::StandardRepository> repository_;
    std::string suffix_;
    std::string user_id_;
};

TEST_F(StandardRepositoryTest, StartupSyncInsertsWithoutOverwritingChecksumConflict) {
    auto technical = manifest(StandardFamily::technical_condition, "SYNC", '1');

    const auto inserted = repository_->sync_package(technical);
    ASSERT_EQ(inserted.status, StandardPackageSyncStatus::Inserted);
    ASSERT_TRUE(inserted.package_id.has_value());

    const auto unchanged = repository_->sync_package(technical);
    EXPECT_EQ(unchanged.status, StandardPackageSyncStatus::Unchanged);
    EXPECT_EQ(unchanged.package_id, inserted.package_id);

    technical.content_checksum = "sha256:" + std::string(64, '9');
    const auto conflict = repository_->sync_package(technical);
    EXPECT_EQ(conflict.status, StandardPackageSyncStatus::ChecksumConflict);
    EXPECT_EQ(conflict.package_id, inserted.package_id);

    const auto stored = repository_->find_package(
        technical.family, technical.standard_id, technical.package_version);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->content_checksum, "sha256:" + std::string(64, '1'));
    EXPECT_THROW(
        client_->execSqlSync(
            "update standard_packages set content_checksum=$2 where id=$1::uuid",
            *inserted.package_id,
            "sha256:" + std::string(64, '8')),
        drogon::orm::DrogonDbException);
}

TEST_F(StandardRepositoryTest, DistinguishesEnabledDisabledFaultAndAdminAuthorization) {
    const auto source = manifest(StandardFamily::technical_condition, "STATE", '3');
    const auto synced = repository_->sync_package(source);
    ASSERT_TRUE(synced.package_id.has_value());

    EXPECT_EQ(
        repository_->set_package_enabled(*synced.package_id, false, "normal"),
        SetStandardPackageEnabledStatus::Forbidden);
    EXPECT_EQ(
        repository_->set_package_enabled(*synced.package_id, false, "admin"),
        SetStandardPackageEnabledStatus::Updated);
    const auto enabled_after_disable = repository_->list_packages(true);
    EXPECT_TRUE(std::none_of(
        enabled_after_disable.begin(), enabled_after_disable.end(),
        [&](const auto& item) { return item.id == *synced.package_id; }));
    auto stored = repository_->find_package(source.family, source.standard_id, source.package_version);
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->is_enabled);
    EXPECT_EQ(stored->sync_status, "正常");

    EXPECT_EQ(
        repository_->set_package_enabled(*synced.package_id, true, "admin"),
        SetStandardPackageEnabledStatus::Updated);
    EXPECT_TRUE(repository_->mark_package_fault(
        *synced.package_id, "package_invalid", "规则包测试故障"));
    stored = repository_->find_package(source.family, source.standard_id, source.package_version);
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->is_enabled);
    EXPECT_EQ(stored->sync_status, "故障");
    EXPECT_EQ(stored->sync_error_code, "package_invalid");
    EXPECT_EQ(
        repository_->set_package_enabled(*synced.package_id, true, "admin"),
        SetStandardPackageEnabledStatus::FaultBlocked);
    const auto enabled_after_fault = repository_->list_packages(true);
    EXPECT_TRUE(std::none_of(
        enabled_after_fault.begin(), enabled_after_fault.end(),
        [&](const auto& item) { return item.id == *synced.package_id; }));
}

TEST_F(StandardRepositoryTest, ProfileRequiresCorrectEnabledFamilies) {
    const auto [technical_id, maintenance_id] = sync_pair();
    const auto other_technical = repository_->sync_package(
        manifest(StandardFamily::technical_condition, "OTHER", '4'));
    ASSERT_TRUE(other_technical.package_id.has_value());

    const auto created = repository_->create_profile(
        profile_request(technical_id, maintenance_id));
    EXPECT_EQ(created.status, CreateStandardProfileStatus::Created);
    ASSERT_TRUE(created.profile.has_value());
    EXPECT_EQ(created.profile->revision_number, 1);

    const auto mismatch = repository_->create_profile(
        profile_request(technical_id, *other_technical.package_id));
    EXPECT_EQ(mismatch.status, CreateStandardProfileStatus::FamilyMismatch);

    ASSERT_EQ(
        repository_->set_package_enabled(maintenance_id, false, "admin"),
        SetStandardPackageEnabledStatus::Updated);
    const auto unavailable = repository_->create_profile(
        profile_request(technical_id, maintenance_id));
    EXPECT_EQ(unavailable.status, CreateStandardProfileStatus::PackageUnavailable);
}

TEST_F(StandardRepositoryTest, FormalProfileIsImmutableButCanCreateNewRevision) {
    const auto [technical_id, maintenance_id] = sync_pair();
    const auto replacement = repository_->sync_package(
        manifest(StandardFamily::technical_condition, "REPLACEMENT", '5'));
    ASSERT_TRUE(replacement.package_id.has_value());
    const auto created = repository_->create_profile(
        profile_request(technical_id, maintenance_id));
    ASSERT_TRUE(created.profile.has_value());

    const auto bridge = client_->execSqlSync(
        "insert into bridges (bridge_name) values ($1) returning id::text as id",
        bridge_name());
    client_->execSqlSync(
        "insert into inspection_years (bridge_id, inspection_year, status, standard_profile_id) "
        "values ($1::uuid, 2026, '已确认', $2::uuid)",
        bridge[0]["id"].as<std::string>(),
        created.profile->id);

    EXPECT_THROW(
        client_->execSqlSync(
            "update project_standard_profiles set change_reason='禁止覆盖' where id=$1::uuid",
            created.profile->id),
        drogon::orm::DrogonDbException);

    const auto revised = repository_->revise_profile(
        created.profile->id,
        profile_request(*replacement.package_id, maintenance_id, "正式结果后的规范修订"));
    EXPECT_EQ(revised.status, CreateStandardProfileStatus::Created);
    ASSERT_TRUE(revised.profile.has_value());
    EXPECT_EQ(revised.profile->profile_series_id, created.profile->profile_series_id);
    EXPECT_EQ(revised.profile->revision_number, 2);
    EXPECT_EQ(revised.profile->supersedes_profile_id, created.profile->id);

    EXPECT_THROW(
        client_->execSqlSync("delete from standard_packages where id=$1::uuid", technical_id),
        drogon::orm::DrogonDbException);
}

TEST_F(StandardRepositoryTest, InspectionRevisionCanInheritProfile) {
    const auto [technical_id, maintenance_id] = sync_pair();
    const auto profile = repository_->create_profile(
        profile_request(technical_id, maintenance_id));
    ASSERT_TRUE(profile.profile.has_value());
    const auto bridge = client_->execSqlSync(
        "insert into bridges (bridge_name) values ($1) returning id::text as id",
        bridge_name());
    const auto source = client_->execSqlSync(
        "insert into inspection_years (bridge_id, inspection_year, is_current, standard_profile_id) "
        "values ($1::uuid, 2026, true, $2::uuid) returning id::text as id",
        bridge[0]["id"].as<std::string>(),
        profile.profile->id);
    const auto revision = client_->execSqlSync(
        "insert into inspection_years (bridge_id, inspection_year, version_number, is_current, "
        "revision_source_inspection_id) values ($1::uuid, 2026, 2, false, $2::uuid) "
        "returning id::text as id",
        bridge[0]["id"].as<std::string>(),
        source[0]["id"].as<std::string>());

    EXPECT_TRUE(repository_->inherit_profile_for_revision(
        source[0]["id"].as<std::string>(),
        revision[0]["id"].as<std::string>()));
    const auto inherited = client_->execSqlSync(
        "select standard_profile_id::text as id from inspection_years where id=$1::uuid",
        revision[0]["id"].as<std::string>());
    EXPECT_EQ(inherited[0]["id"].as<std::string>(), profile.profile->id);
}

}  // namespace
