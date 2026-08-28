#include <cstdlib>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ImportResolutionRepository.hpp"

namespace {

using bridge_report::db::ImportResolutionRepository;
using bridge_report::resolution::ComponentGroupMember;
using bridge_report::resolution::ComponentResolutionGroup;
using bridge_report::resolution::ComponentResolutionTarget;
using bridge_report::resolution::ResolvedDefectInstance;

class ImportResolutionRepositoryTest : public testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL is not set";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{});
        user_id_ = client_->execSqlSync(
            "select id::text from users where username='admin'")[0]["id"].as<std::string>();
        bridge_id_ = client_->execSqlSync(
            "insert into bridges (bridge_name) values ('解析仓储测试桥') returning id::text"
        )[0]["id"].as<std::string>();
        revision_id_ = client_->execSqlSync(
            "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
            "created_by_user_id) values($1::uuid,1,$2::uuid) returning id::text",
            bridge_id_, user_id_)[0]["id"].as<std::string>();
        for (int index = 1; index <= 3; ++index) {
            const auto number = std::to_string(index) + "#伸缩缝";
            const auto component_id = client_->execSqlSync(
                "insert into bridge_components(bridge_id,structure_part,component_type,"
                "business_component_code,normalized_component_key) "
                "values($1::uuid,'桥面系','伸缩缝',$2,$3) returning id::text",
                bridge_id_, number, "resolution-repo-" + std::to_string(index)
            )[0]["id"].as<std::string>();
            component_ids_.push_back(component_id);
            client_->execSqlSync(
                "insert into bridge_component_inventory_entries(inventory_revision_id,"
                "bridge_component_id,component_number,site_name,site_component_type,sort_order) "
                "values($1::uuid,$2::uuid,$3,'伸缩缝','伸缩缝',$4)",
                revision_id_, component_id, number, index);
        }
        client_->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认',"
            "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
            revision_id_, user_id_);
        import_id_ = client_->execSqlSync(
            "insert into import_records(bridge_id,import_name,source_type,import_status) "
            "values($1::uuid,'解析仓储测试导入','接口同步','待校对') returning id::text",
            bridge_id_)[0]["id"].as<std::string>();
    }

    void TearDown() override {
        if (!client_) return;
        client_->execSqlSync("delete from import_records where id=$1::uuid", import_id_);
        client_->execSqlSync(
            "delete from bridge_component_inventory_revisions where bridge_id=$1::uuid",
            bridge_id_);
        client_->execSqlSync("delete from bridge_components where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        client_->closeAll();
    }

    /// 一个已绑定的组 + 一条来源病害 + 三个目标 + 三条实例（区间展开的形状）。
    ///
    /// 必须整段在一个事务里：组状态与目标集合的一致性是延迟约束，逐条自动提交时
    /// "先写 bound 组、还没有目标"那一步会当场被挡下来。生产代码里这几步本来就同在
    /// 导入/绑定事务内，测试没有理由用一种线上不存在的写法。
    ComponentGroupMember seed_expanded_member() {
        const auto latch = std::make_shared<bridge_report::db::CommitLatch>();
        auto tx = client_->newTransaction(latch->callback());
        ComponentGroupMember stored_member;
        // 仓库对象必须活在内层作用域里。它的构造函数按值收下 DbClientPtr 并一直持有，
        // 留一个具名变量与 tx 同域，就会让事务的 shared_ptr 活过下面的 tx.reset()，
        // 提交回调永远不来，最后以 30 秒超时收场（WordImportRepository.cpp 同款陷阱）。
        {
            const ImportResolutionRepository repository(tx);
            ComponentResolutionGroup group;
            group.import_record_id = import_id_;
            group.source_component_name = "伸缩缝";
            group.source_component_number = "1~3#伸缩缝";
            group.normalized_component_number = "1~3#伸缩缝";
            group.resolution_mode = "range";
            group.status = "bound";
            group.match_method = "range";
            group.inventory_revision_id = revision_id_;
            group.resolved_by_user_id = user_id_;
            const auto stored_group = repository.insert_group(group);

            ComponentGroupMember member;
            member.import_record_id = import_id_;
            member.group_id = stored_group.id;
            member.source_candidate_id = "source_defect_0123";
            member.source_order = 0;
            stored_member = repository.insert_member(member);

            for (int index = 0; index < 3; ++index) {
                ComponentResolutionTarget target;
                target.group_id = stored_group.id;
                target.bridge_component_id = component_ids_[index];
                target.target_order = index + 1;
                target.target_role = "range_member";
                const auto stored_target = repository.insert_target(target);

                ResolvedDefectInstance instance;
                instance.group_member_id = stored_member.id;
                instance.target_id = stored_target.id;
                instance.instance_order = index + 1;
                instance.component_resolution_version = stored_group.version;
                repository.insert_instance(instance);
            }
            repository.recompute_photo_owner(stored_member.id);
        }
        tx.reset();
        EXPECT_TRUE(latch->wait()) << "解析状态种子事务未提交";
        return stored_member;
    }

    std::vector<ResolvedDefectInstance> instances_of(const std::string& member_id) {
        return ImportResolutionRepository(client_).list_instances_by_member(member_id);
    }

    void set_status(const ResolvedDefectInstance& instance, const std::string& status) {
        const auto version = ImportResolutionRepository(client_).set_instance_status(
            instance.id, instance.version, status);
        ASSERT_TRUE(version.has_value()) << "实例状态切换落空：" << instance.id;
    }

    drogon::orm::DbClientPtr client_;
    std::string user_id_;
    std::string bridge_id_;
    std::string revision_id_;
    std::string import_id_;
    std::vector<std::string> component_ids_;
};

}  // namespace

TEST_F(ImportResolutionRepositoryTest, PhotoOwnerIsTheLowestActiveInstance) {
    const auto member = seed_expanded_member();

    const auto instances = instances_of(member.id);
    ASSERT_EQ(instances.size(), 3u);
    EXPECT_TRUE(instances[0].is_photo_owner);
    EXPECT_FALSE(instances[1].is_photo_owner);
    EXPECT_FALSE(instances[2].is_photo_owner);
}

// 归属者不是"instance_order = 1 的活动实例"。按后者写的话，实例 1 一被忽略，
// 整组照片就没有归属者，正式入库时凭空消失。
TEST_F(ImportResolutionRepositoryTest, IgnoringTheOwnerMovesPhotosToTheNextActiveInstance) {
    const auto member = seed_expanded_member();

    auto instances = instances_of(member.id);
    set_status(instances[0], "ignored");

    instances = instances_of(member.id);
    EXPECT_FALSE(instances[0].is_photo_owner);
    EXPECT_TRUE(instances[1].is_photo_owner);
    EXPECT_FALSE(instances[2].is_photo_owner);

    // 撤销忽略后归属回到最小序号。
    set_status(instances[0], "active");
    instances = instances_of(member.id);
    EXPECT_TRUE(instances[0].is_photo_owner);
    EXPECT_FALSE(instances[1].is_photo_owner);
}

TEST_F(ImportResolutionRepositoryTest, NobodyOwnsPhotosWhenEveryInstanceIsIgnored) {
    const auto member = seed_expanded_member();

    for (const auto& instance : instances_of(member.id)) {
        set_status(instance, "ignored");
    }

    for (const auto& instance : instances_of(member.id)) {
        EXPECT_FALSE(instance.is_photo_owner);
    }
}

// 实例覆盖与状态切换用实例自身的 version，不能拿 component_resolution_version 顶替：
// 后者说明的是"依据的是哪一代构件解析"，两者含义不同（§15）。
TEST_F(ImportResolutionRepositoryTest, StaleInstanceVersionWritesNothing) {
    const auto member = seed_expanded_member();
    const ImportResolutionRepository repository(client_);
    const auto instance = instances_of(member.id)[1];
    ASSERT_EQ(instance.version, 1);

    ASSERT_TRUE(repository.set_instance_status(instance.id, 1, "ignored").has_value());
    EXPECT_FALSE(repository.set_instance_status(instance.id, 1, "active").has_value());

    EXPECT_EQ(instances_of(member.id)[1].instance_status, "ignored");
}

// 版本条件写而不是"先读再写"：两个标签页同时提交时，后一条必须落空而不是覆盖。
TEST_F(ImportResolutionRepositoryTest, StaleGroupVersionWritesNothing) {
    const ImportResolutionRepository repository(client_);
    ComponentResolutionGroup group;
    group.import_record_id = import_id_;
    group.source_component_name = "伸缩缝";
    group.normalized_component_number = "1";
    const auto stored = repository.insert_group(group);
    ASSERT_EQ(stored.version, 1);

    const auto first = repository.update_group_resolution(
        stored.id, 1, "missing", std::nullopt, std::nullopt, "single", user_id_);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, 2);

    // 拿着旧版本再提交一次：必须落空，组状态保持第一次的结果。
    const auto stale = repository.update_group_resolution(
        stored.id, 1, "unresolved", std::nullopt, std::nullopt, "single", std::nullopt);
    EXPECT_FALSE(stale.has_value());

    const auto reread = repository.find_group(stored.id);
    ASSERT_TRUE(reread.has_value());
    EXPECT_EQ(reread->status, "missing");
    EXPECT_EQ(reread->version, 2);
}

TEST_F(ImportResolutionRepositoryTest, DraftVersionBumpsOnlyWithTheExpectedVersion) {
    const ImportResolutionRepository repository(client_);

    const auto initial = repository.read_draft_version(import_id_);
    ASSERT_TRUE(initial.has_value());
    EXPECT_EQ(*initial, 1);

    const auto bumped = repository.bump_draft_version(import_id_, 1);
    ASSERT_TRUE(bumped.has_value());
    EXPECT_EQ(*bumped, 2);

    // 陈旧的整份草稿不能把手工新增刚写进去的来源病害当成"用户删掉了"。
    EXPECT_FALSE(repository.bump_draft_version(import_id_, 1).has_value());
    EXPECT_EQ(*repository.read_draft_version(import_id_), 2);
}

TEST_F(ImportResolutionRepositoryTest, EmptyGroupsAreDeletedAfterTheirLastMemberLeaves) {
    const ImportResolutionRepository repository(client_);
    ComponentResolutionGroup group;
    group.import_record_id = import_id_;
    group.source_component_name = "桥面铺装";
    group.normalized_component_number = "";
    const auto stored_group = repository.insert_group(group);

    ComponentGroupMember member;
    member.import_record_id = import_id_;
    member.group_id = stored_group.id;
    member.source_candidate_id = "source_defect_0001";
    member.source_order = 0;
    const auto stored_member = repository.insert_member(member);

    EXPECT_EQ(repository.delete_empty_groups(import_id_), 0);

    repository.delete_member(stored_member.id);
    EXPECT_EQ(repository.delete_empty_groups(import_id_), 1);
    EXPECT_FALSE(repository.find_group(stored_group.id).has_value());
}
