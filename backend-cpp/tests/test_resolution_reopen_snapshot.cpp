#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/CommitLatch.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/EditLockRepository.hpp"
#include "bridge_report/db/ReviewRepository.hpp"
#include "bridge_report/db/ImportResolutionRepository.hpp"
#include "bridge_report/db/WordImportRepository.hpp"
#include "bridge_report/resolution/ResolutionReopenSnapshot.hpp"

// 重开校对的关系态快照（设计 §8.8）。
//
// 这一组用例守的是"两半状态一起走"：来源草稿与解析状态必须在同一个世代边界上一起
// 保存、一起还原，且三个边界都要把未执行的计划作废——快照还原会把对象版本号也还原
// 成计划创建时的值，光靠版本比对拦不住旧计划。

namespace {

using bridge_report::db::AuthUser;
using bridge_report::db::CommitLatch;
using bridge_report::db::ImportResolutionRepository;
using bridge_report::resolution::capture_reopen_snapshot;
using bridge_report::resolution::discard_reopen_snapshot;
using bridge_report::resolution::restore_reopen_snapshot;

class ResolutionReopenSnapshotTest : public testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL is not set";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{});
        user_id_ = client_->execSqlSync(
            "select id::text from users where username='admin'")[0]["id"].as<std::string>();
        bridge_id_ = client_->execSqlSync(
            "insert into bridges (bridge_name) values ('重开快照测试桥') returning id::text"
        )[0]["id"].as<std::string>();
        year_id_ = client_->execSqlSync(
            "insert into inspection_years (bridge_id, inspection_year, status, is_current) "
            "values ($1::uuid, 2026, '待校对', true) returning id::text",
            bridge_id_)[0]["id"].as<std::string>();
        import_id_ = client_->execSqlSync(
            "insert into import_records (bridge_id, inspection_year_id, import_name, "
            "source_type, import_status) values ($1::uuid, $2::uuid, '重开快照测试', "
            "'接口同步', '解析中') returning id::text",
            bridge_id_, year_id_)[0]["id"].as<std::string>();
        source_file_id_ = client_->execSqlSync(
            "insert into import_source_files (import_record_id, original_file_name,"
            "storage_relative_path, file_extension, file_size_bytes, file_hash, status,"
            "parsing_started_at) values ($1::uuid,'sync.srcref',$1::text||'.srcref',"
            "'.srcref',9,$2,'解析中',now()) returning id::text",
            import_id_, std::string(64, 'a'))[0]["id"].as<std::string>();
    }

    void TearDown() override {
        if (!client_) return;
        client_->execSqlSync("delete from import_source_files where id=$1::uuid", source_file_id_);
        client_->execSqlSync("delete from import_records where id=$1::uuid", import_id_);
        client_->execSqlSync(
            "update inspection_years set component_inventory_revision_id=null where id=$1::uuid",
            year_id_);
        client_->execSqlSync(
            "delete from bridge_component_inventory_revisions where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from bridge_components where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where id=$1::uuid", year_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        if (!package_id_.empty()) {
            client_->execSqlSync("delete from standard_packages where id=$1::uuid", package_id_);
        }
        client_->closeAll();
    }

    void seed_confirmed_inventory(const std::vector<std::string>& numbers) {
        package_id_ = client_->execSqlSync(
            "insert into standard_packages(standard_family,standard_id,standard_code,"
            "standard_name,official_edition,package_version,contract_version,algorithm_id,"
            "effective_date,content_checksum) values('technical_condition',"
            "'RS-'||gen_random_uuid()::text,'RS','重开快照测试规范','2026','1.0.0',1,'rs',"
            "'2026-01-01','sha256:'||repeat('d',64)) returning id::text"
        )[0]["id"].as<std::string>();
        revision_id_ = client_->execSqlSync(
            "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
            "created_by_user_id) values($1::uuid,1,$2::uuid) returning id::text",
            bridge_id_, user_id_)[0]["id"].as<std::string>();
        int order = 1;
        for (const auto& number : numbers) {
            const auto component_id = client_->execSqlSync(
                "insert into bridge_components(bridge_id,structure_part,component_type,"
                "business_component_code,normalized_component_key) "
                "values($1::uuid,'上部结构','主梁',$2,$3) returning id::text",
                bridge_id_, number, "rs-" + std::to_string(order))[0]["id"].as<std::string>();
            const auto entry_id = client_->execSqlSync(
                "insert into bridge_component_inventory_entries(inventory_revision_id,"
                "bridge_component_id,component_number,site_name,site_component_type,sort_order) "
                "values($1::uuid,$2::uuid,$3,'空心板','空心板',$4) returning id::text",
                revision_id_, component_id, number, order)[0]["id"].as<std::string>();
            client_->execSqlSync(
                "insert into bridge_component_standard_mappings(inventory_entry_id,"
                "standard_package_id,standard_bridge_type_id,standard_component_category_id,"
                "structure_part,mapping_source,confirmation_status,confirmed_by_user_id,"
                "confirmed_at) values($1::uuid,$2::uuid,'h21.bridge_type.beam',"
                "'h21.component.beam.upper_bearing','superstructure','规范模板','已确认',"
                "$3::uuid,now())",
                entry_id, package_id_, user_id_);
            ++order;
        }
        client_->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认',"
            "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
            revision_id_, user_id_);
    }

    void import_defects(const std::vector<std::string>& numbers) {
        bridge_report::archive::ArchivedPhotoBatch batch;
        batch.data["contract"]["parser_name"] = "source-db-importer";
        batch.data["contract"]["parser_version"] = "1.0.0";
        batch.data["photos"] = Json::Value(Json::arrayValue);
        int index = 1;
        for (const auto& number : numbers) {
            Json::Value defect(Json::objectValue);
            defect["candidate_id"] = "source_defect_" + std::to_string(index);
            defect["component_name"] = "上部承重构件";
            defect["component_number"] = number;
            defect["defect_type"] = "裂缝";
            defect["defect_location"] = "底板";
            defect["defect_description"] = "底板出现纵向裂缝";
            defect["warnings"] = Json::Value(Json::arrayValue);
            batch.data["defects"].append(defect);
            ++index;
        }
        const auto outcome =
            bridge_report::db::WordImportRepository(client_).persist_parse_result(import_id_, batch);
        ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    }

    /// 在一个真事务里跑一段操作。仓库对象一律用临时量，理由见
    /// WordImportRepository.cpp 里那段注释。
    template <typename Body>
    void in_transaction(Body&& body) {
        std::shared_ptr<drogon::orm::Transaction> tx;
        auto latch = std::make_shared<CommitLatch>();
        tx = client_->newTransaction(latch->callback());
        body(tx);
        tx.reset();
        ASSERT_TRUE(latch->wait());
    }

    std::string group_status() {
        return client_->execSqlSync(
            "select status from import_component_resolution_groups where import_record_id=$1::uuid",
            import_id_)[0]["status"].as<std::string>();
    }

    int target_count() {
        return client_->execSqlSync(
            "select count(*)::int as n from import_component_resolution_targets t "
            "join import_component_resolution_groups g on g.id=t.group_id "
            "where g.import_record_id=$1::uuid",
            import_id_)[0]["n"].as<int>();
    }

    std::string first_group_id() {
        return client_->execSqlSync(
            "select id::text as id from import_component_resolution_groups "
            "where import_record_id=$1::uuid order by source_component_name limit 1",
            import_id_)[0]["id"].as<std::string>();
    }

    /// 建一条 ready 计划。内容不重要，这里只关心它在世代边界上会不会被作废。
    std::string seed_ready_plan() {
        return client_->execSqlSync(
            "insert into import_resolution_operation_plans(import_record_id,actor_user_id,"
            "operation_type,lock_token_hash,request_json,plan_json,preconditions_json,"
            "status,expires_at) values($1::uuid,$2::uuid,'bulk_replace',$3,'{}'::jsonb,"
            "'{}'::jsonb,'{}'::jsonb,'ready',now()+interval '15 minutes') returning id::text",
            import_id_, user_id_, std::string(64, 'e'))[0]["id"].as<std::string>();
    }

    std::string plan_status(const std::string& plan_id) {
        return client_->execSqlSync(
            "select status from import_resolution_operation_plans where id=$1::uuid",
            plan_id)[0]["status"].as<std::string>();
    }

    int event_count(const std::string& operation_type) {
        return client_->execSqlSync(
            "select count(*)::int as n from import_resolution_events "
            "where import_record_id=$1::uuid and operation_type=$2",
            import_id_, operation_type)[0]["n"].as<int>();
    }

    drogon::orm::DbClientPtr client_;
    std::string user_id_;
    std::string bridge_id_;
    std::string year_id_;
    std::string import_id_;
    std::string source_file_id_;
    std::string revision_id_;
    std::string package_id_;
};

}  // namespace

// 重开时绑定是什么样，放弃修改后就得还是什么样。此前只还原来源 JSON，绑定留在
// 重开期间改成的状态——病害文字回到确认时的样子，绑定却没有。
TEST_F(ResolutionReopenSnapshotTest, RestoresTheBindingTheReopenStartedFrom) {
    seed_confirmed_inventory({"1-1#梁"});
    import_defects({"1-1#梁"});
    ASSERT_EQ(group_status(), "bound");
    ASSERT_EQ(target_count(), 1);

    in_transaction([&](const auto& tx) {
        const auto captured = capture_reopen_snapshot(tx, import_id_, user_id_);
        ASSERT_TRUE(captured.success);
        EXPECT_FALSE(captured.checksum.empty());
    });

    // 重开期间把这一组解绑：目标没了，组回到 unresolved。
    const auto group_id = first_group_id();
    in_transaction([&](const auto& tx) {
        ImportResolutionRepository(tx).delete_targets(group_id);
        tx->execSqlSync(
            "update import_component_resolution_groups "
            "set status='unresolved', match_method=null, version=version+1 where id=$1::uuid",
            group_id);
    });
    ASSERT_EQ(group_status(), "unresolved");
    ASSERT_EQ(target_count(), 0);

    in_transaction([&](const auto& tx) {
        const auto restored = restore_reopen_snapshot(tx, import_id_, user_id_);
        ASSERT_TRUE(restored.success);
        EXPECT_FALSE(restored.checksum.empty());
    });

    EXPECT_EQ(group_status(), "bound");
    EXPECT_EQ(target_count(), 1);
    EXPECT_EQ(event_count("reopen_snapshot_restored"), 1);
    // 还原后快照即失效，不能留着被第二次"放弃修改"重放。
    EXPECT_EQ(
        client_->execSqlSync(
            "select count(*)::int as n from import_resolution_reopen_snapshots "
            "where import_record_id=$1::uuid", import_id_)[0]["n"].as<int>(),
        0);
}

// 快照还原会把 version 一起还原成计划创建时的值，因此"版本对得上"根本挡不住旧计划。
// 三个世代边界都必须显式作废 ready 计划。
TEST_F(ResolutionReopenSnapshotTest, EveryGenerationBoundaryInvalidatesReadyPlans) {
    seed_confirmed_inventory({"1-1#梁"});
    import_defects({"1-1#梁"});

    const auto on_reopen = seed_ready_plan();
    in_transaction([&](const auto& tx) {
        EXPECT_EQ(capture_reopen_snapshot(tx, import_id_, user_id_).invalidated_plan_count, 1);
    });
    EXPECT_EQ(plan_status(on_reopen), "invalidated");

    const auto on_restore = seed_ready_plan();
    in_transaction([&](const auto& tx) {
        EXPECT_EQ(restore_reopen_snapshot(tx, import_id_, user_id_).invalidated_plan_count, 1);
    });
    EXPECT_EQ(plan_status(on_restore), "invalidated");

    const auto on_reconfirm = seed_ready_plan();
    in_transaction([&](const auto& tx) {
        EXPECT_EQ(discard_reopen_snapshot(tx, import_id_, user_id_).invalidated_plan_count, 1);
    });
    EXPECT_EQ(plan_status(on_reconfirm), "invalidated");

    // 作废原因要能分辨是哪一个边界干的，否则审计时读不出经过。
    const auto reasons = client_->execSqlSync(
        "select invalidated_reason from import_resolution_operation_plans "
        "where import_record_id=$1::uuid order by created_at", import_id_);
    ASSERT_EQ(reasons.size(), 3u);
    EXPECT_EQ(reasons[0]["invalidated_reason"].as<std::string>(), "import_record_reopened");
    EXPECT_EQ(reasons[1]["invalidated_reason"].as<std::string>(), "reopen_snapshot_restored");
    EXPECT_EQ(reasons[2]["invalidated_reason"].as<std::string>(), "import_record_reconfirmed");
}

// 重新确认成功即退出重开态，快照不再需要；留着它下一次重开会被误当成上一代状态。
TEST_F(ResolutionReopenSnapshotTest, ReconfirmingDropsTheSnapshot) {
    seed_confirmed_inventory({"1-1#梁"});
    import_defects({"1-1#梁"});
    in_transaction([&](const auto& tx) {
        ASSERT_TRUE(capture_reopen_snapshot(tx, import_id_, user_id_).success);
    });

    in_transaction([&](const auto& tx) {
        const auto discarded = discard_reopen_snapshot(tx, import_id_, user_id_);
        ASSERT_TRUE(discarded.success);
        EXPECT_FALSE(discarded.checksum.empty());
    });

    EXPECT_EQ(
        client_->execSqlSync(
            "select count(*)::int as n from import_resolution_reopen_snapshots "
            "where import_record_id=$1::uuid", import_id_)[0]["n"].as<int>(),
        0);
    EXPECT_EQ(event_count("reopen_snapshot_discarded"), 1);
}

// 改造之前重开的记录没有快照。那种记录的来源 JSON 仍要照常还原，关系态维持现状——
// 把它当失败会让"放弃修改"整个卡死，把关系态清空则比不还原更糟。
TEST_F(ResolutionReopenSnapshotTest, MissingSnapshotLeavesTheRelationalStateAlone) {
    seed_confirmed_inventory({"1-1#梁"});
    import_defects({"1-1#梁"});
    const auto plan_id = seed_ready_plan();

    in_transaction([&](const auto& tx) {
        const auto restored = restore_reopen_snapshot(tx, import_id_, user_id_);
        EXPECT_TRUE(restored.success);
        EXPECT_TRUE(restored.checksum.empty());
    });

    EXPECT_EQ(group_status(), "bound");
    EXPECT_EQ(target_count(), 1);
    // 世代仍然变了（来源 JSON 被换掉），计划照样作废。
    EXPECT_EQ(plan_status(plan_id), "invalidated");
    EXPECT_EQ(event_count("reopen_snapshot_restored"), 0);
}

// 重复重开覆盖上一份快照，而不是堆叠——表上是每条记录至多一条。
TEST_F(ResolutionReopenSnapshotTest, CapturingTwiceKeepsOnlyTheLatestSnapshot) {
    seed_confirmed_inventory({"1-1#梁", "1-2#梁"});
    import_defects({"1-1#梁", "1-2#梁"});

    std::string first_checksum;
    in_transaction([&](const auto& tx) {
        first_checksum = capture_reopen_snapshot(tx, import_id_, user_id_).checksum;
    });

    const auto group_id = first_group_id();
    in_transaction([&](const auto& tx) {
        ImportResolutionRepository(tx).delete_targets(group_id);
        tx->execSqlSync(
            "update import_component_resolution_groups "
            "set status='unresolved', match_method=null where id=$1::uuid",
            group_id);
    });

    std::string second_checksum;
    in_transaction([&](const auto& tx) {
        second_checksum = capture_reopen_snapshot(tx, import_id_, user_id_).checksum;
    });

    EXPECT_NE(first_checksum, second_checksum);
    const auto rows = client_->execSqlSync(
        "select checksum from import_resolution_reopen_snapshots where import_record_id=$1::uuid",
        import_id_);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0]["checksum"].as<std::string>(), second_checksum);
}

// 验收标准 14：已确认记录重开后，放弃修改要**同时**恢复来源草稿和解析状态。
// 这一条走真实路径（acquire_and_reopen → restore_reopened_import_record），而不是直接叫
// 快照函数：两半状态是不是真在一个事务里，只有从这个层面才看得出来。
TEST_F(ResolutionReopenSnapshotTest, DiscardingReopenRestoresBothHalvesTogether) {
    seed_confirmed_inventory({"1-1#梁"});
    import_defects({"1-1#梁"});
    const auto confirmed_json = client_->execSqlSync(
        "select parsed_result_json::text as json from import_records where id=$1::uuid",
        import_id_)[0]["json"].as<std::string>();
    client_->execSqlSync(
        "update import_records set import_status='已确认' where id=$1::uuid", import_id_);

    AuthUser user;
    user.id = user_id_;
    user.session_id = client_->execSqlSync(
        "insert into user_sessions(user_id,token_hash,expires_at) "
        "values($1::uuid,$2,now()+interval '1 hour') returning id::text",
        user_id_, std::string(64, 'b'))[0]["id"].as<std::string>();
    user.username = "admin";
    user.role = "admin";

    bridge_report::db::EditLockRepository locks(client_);
    const auto reopened = locks.acquire_and_reopen(import_id_, user, "full");
    ASSERT_TRUE(reopened.acquired);
    ASSERT_EQ(group_status(), "bound");

    // 重开期间两半各改一处：来源病害文字，以及构件绑定。
    client_->execSqlSync(
        "update import_records set parsed_result_json = "
        "jsonb_set(parsed_result_json,'{defects,0,defect_description}',"
        "'\"重开期间改过的描述\"'::jsonb) where id=$1::uuid",
        import_id_);
    const auto group_id = first_group_id();
    in_transaction([&](const auto& tx) {
        ImportResolutionRepository(tx).delete_targets(group_id);
        tx->execSqlSync(
            "update import_component_resolution_groups "
            "set status='unresolved', match_method=null, version=version+1 where id=$1::uuid",
            group_id);
    });
    ASSERT_EQ(group_status(), "unresolved");

    bridge_report::db::EditLockCredentials credentials{
        user.id, user.session_id, reopened.lock_token};
    ASSERT_TRUE(
        bridge_report::db::ReviewRepository(client_)
            .restore_reopened_import_record(import_id_, credentials));

    // 两半一起回来。只回滚其中一半的话，下面两条断言会一对一错。
    const auto restored_json = client_->execSqlSync(
        "select parsed_result_json::text as json, import_status, reopened_at::text as reopened_at "
        "from import_records where id=$1::uuid", import_id_);
    EXPECT_EQ(restored_json[0]["json"].as<std::string>(), confirmed_json);
    EXPECT_EQ(restored_json[0]["import_status"].as<std::string>(), "已确认");
    EXPECT_TRUE(restored_json[0]["reopened_at"].isNull());
    EXPECT_EQ(group_status(), "bound");
    EXPECT_EQ(target_count(), 1);

    client_->execSqlSync("delete from user_sessions where id=$1::uuid", user.session_id);
}
