#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include <drogon/orm/Exception.h>
#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/InspectionYearDeletionRepository.hpp"

namespace {

class InspectionYearDeletionRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{}, 1);
        bridge_id_ = id("insert into bridges(bridge_name) values('删除事务测试桥') returning id");
        year_2025_ = id("insert into inspection_years(bridge_id,inspection_year,status) values($1::uuid,2025,'已确认') returning id", bridge_id_);
        year_v1_ = id("insert into inspection_years(bridge_id,inspection_year,version_number,is_current,status) values($1::uuid,2026,1,false,'已被修订') returning id", bridge_id_);
        year_v2_ = id("insert into inspection_years(bridge_id,inspection_year,version_number,is_current,status,revision_source_inspection_id) values($1::uuid,2026,2,true,'已确认',$2::uuid) returning id", bridge_id_, year_v1_);
        component_id_ = id(
            "insert into bridge_components(bridge_id,structure_part,component_type,business_component_code,normalized_component_key) "
            "values($1::uuid,'上部结构','2-1#板','上部承重构件','delete-test') returning id", bridge_id_);
        thread_id_ = id(
            "insert into defect_threads(bridge_id,bridge_component_id,thread_name,defect_type,first_seen_inspection_id,latest_seen_inspection_id) "
            "values($1::uuid,$2::uuid,'裂缝｜端部','裂缝',$3::uuid,$4::uuid) returning id",
            bridge_id_, component_id_, year_2025_, year_v2_);
        observation_2025_ = observation(year_2025_);
        observation_2026_ = observation(year_v2_);
        client_->execSqlSync("update defect_observations set defect_thread_id=$2::uuid where id in($1::uuid,$3::uuid)", observation_2025_, thread_id_, observation_2026_);
        client_->execSqlSync(
            "insert into defect_measurements(defect_observation_id,measurement_type,numeric_value,raw_text) values($1::uuid,'数量',2,'2处')", observation_2026_);
        client_->execSqlSync(
            "insert into defect_comparisons(bridge_id,current_inspection_year_id,compared_inspection_year_id,comparison_result) values($1::uuid,$2::uuid,$3::uuid,'延续')",
            bridge_id_, year_v2_, year_2025_);
        exclusive_file_id_ = archived_file(year_v2_, "delete-tests/exclusive.docx");
        shared_file_id_ = archived_file(year_v2_, "delete-tests/shared.docx");
        client_->execSqlSync("insert into bridge_aliases(bridge_id,alias_name,source_file_id) values($1::uuid,'删除测试别名',$2::uuid)", bridge_id_, shared_file_id_);
        import_id_ = id(
            "insert into import_records(bridge_id,inspection_year_id,import_name,source_type,main_file_id) values($1::uuid,$2::uuid,'删除测试导入','正式Word',$3::uuid) returning id",
            bridge_id_, year_v2_, exclusive_file_id_);
        temporary_source_id_ = id(
            "with source_id as(select gen_random_uuid() id) "
            "insert into import_source_files(id,import_record_id,original_file_name,storage_relative_path,"
            "file_extension,file_size_bytes,file_hash,status) "
            "select id,$1::uuid,'临时报告.docx',id::text||'.docx','.docx',4,$2,'解析失败' from source_id returning id",
            import_id_, std::string(64, 'c'));
        user_id_ = id(
            "insert into users(username,display_name,password_hash,role) values('delete-test-'||gen_random_uuid()::text,'删除测试员','x','admin') returning id");
    }

    void TearDown() override {
        if (!client_ || bridge_id_.empty()) return;
        if (!user_id_.empty()) {
            client_->execSqlSync("delete from inspection_year_deletion_audits where actor_user_id=$1::uuid", user_id_);
            client_->execSqlSync("delete from user_sessions where user_id=$1::uuid", user_id_);
            client_->execSqlSync("delete from users where id=$1::uuid", user_id_);
        }
        client_->execSqlSync("delete from defect_comparisons where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from import_records where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from import_source_files where id=$1::uuid", temporary_source_id_);
        client_->execSqlSync("delete from defect_observations where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from condition_ratings where inspection_year_id in(select id from inspection_years where bridge_id=$1::uuid)", bridge_id_);
        client_->execSqlSync("delete from defect_threads where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("update inspection_years set revision_source_inspection_id=null,previous_inspection_id=null where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        if (!exclusive_file_id_.empty() && !shared_file_id_.empty())
            client_->execSqlSync("delete from archived_files where id in($1::uuid,$2::uuid)", exclusive_file_id_, shared_file_id_);
        client_->closeAll();
    }

    template <typename... Args>
    std::string id(const std::string& sql, Args&&... args) {
        return client_->execSqlSync(sql, std::forward<Args>(args)...)[0]["id"].template as<std::string>();
    }
    std::string observation(const std::string& year) {
        return id("insert into defect_observations(inspection_year_id,bridge_id,bridge_component_id,structure_part,defect_type,defect_description_raw) values($1::uuid,$2::uuid,$3::uuid,'上部结构','裂缝','裂缝描述') returning id", year, bridge_id_, component_id_);
    }
    std::string archived_file(const std::string& year, const std::string& path) {
        return id("insert into archived_files(bridge_id,inspection_year_id,original_file_name,current_file_name,storage_relative_path,file_type,file_purpose) values($1::uuid,$2::uuid,'a.docx','a.docx',$3,'Word文档','删除测试') returning id", bridge_id_, year, path);
    }
    bridge_report::deletion::DeletionActorSnapshot actor() const { return {user_id_, "delete-test", "删除测试员"}; }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_, year_2025_, year_v1_, year_v2_, component_id_, thread_id_;
    std::string observation_2025_, observation_2026_, exclusive_file_id_, shared_file_id_, import_id_, temporary_source_id_, user_id_;
};

TEST_F(InspectionYearDeletionRepositoryTest, DeletesAllVersionsAndRetainsOtherYearThreadAndSharedFile) {
    bridge_report::db::InspectionYearDeletionRepository repository(client_);
    const auto preview = repository.preview(year_v2_);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->counts.inspection_versions, 2);
    EXPECT_EQ(preview->counts.import_records, 1);
    EXPECT_EQ(preview->counts.defect_observations, 1);
    EXPECT_EQ(preview->counts.defect_measurements, 1);
    EXPECT_EQ(preview->counts.condition_ratings, 0);
    EXPECT_EQ(preview->counts.defect_comparisons, 1);
    EXPECT_EQ(preview->counts.archived_files_to_delete, 1);
    EXPECT_EQ(preview->counts.temporary_source_files_to_delete, 1);
    EXPECT_EQ(preview->counts.shared_files_retained, 1);

    const auto outcome = repository.delete_year(year_v1_, preview->impact_token(), "永久删除 2026", "误建年度", actor());
    ASSERT_EQ(outcome.status, bridge_report::deletion::DeleteInspectionYearStatus::Deleted);
    EXPECT_EQ(outcome.next_inspection_year_id, year_2025_);
    EXPECT_TRUE(client_->execSqlSync("select 1 from inspection_years where bridge_id=$1::uuid and inspection_year=2026", bridge_id_).empty());
    EXPECT_FALSE(client_->execSqlSync("select 1 from defect_threads where id=$1::uuid", thread_id_).empty());
    EXPECT_FALSE(client_->execSqlSync("select 1 from archived_files where id=$1::uuid", shared_file_id_).empty());
    EXPECT_TRUE(client_->execSqlSync("select 1 from archived_files where id=$1::uuid", exclusive_file_id_).empty());
    const auto temporary = client_->execSqlSync(
        "select import_record_id,status,cleanup_reason from import_source_files where id=$1::uuid",
        temporary_source_id_);
    ASSERT_EQ(temporary.size(), 1u);
    EXPECT_TRUE(temporary[0]["import_record_id"].isNull());
    EXPECT_EQ(temporary[0]["status"].as<std::string>(), "待清理");
    EXPECT_EQ(temporary[0]["cleanup_reason"].as<std::string>(), "业务删除");
}

TEST_F(InspectionYearDeletionRepositoryTest, ActiveEditLockBlocksAndChangedImpactTokenRequiresReconfirmation) {
    bridge_report::db::InspectionYearDeletionRepository repository(client_);
    const auto preview = repository.preview(year_v2_);
    ASSERT_TRUE(preview.has_value());

    const auto session_id = id("insert into user_sessions(user_id,token_hash,expires_at) values($1::uuid,gen_random_uuid()::text,now()+interval '1 hour') returning id", user_id_);
    client_->execSqlSync(
        "insert into import_record_edit_locks(import_record_id,user_id,user_session_id,lock_token_hash,expires_at) values($1::uuid,$2::uuid,$3::uuid,gen_random_uuid()::text,now()+interval '10 minutes')",
        import_id_, user_id_, session_id);
    const auto locked = repository.delete_year(year_v2_, preview->impact_token(), "永久删除 2026", "误建年度", actor());
    EXPECT_EQ(locked.status, bridge_report::deletion::DeleteInspectionYearStatus::Locked);

    client_->execSqlSync("delete from import_record_edit_locks where import_record_id=$1::uuid", import_id_);
    client_->execSqlSync(
        "insert into defect_measurements(defect_observation_id,measurement_type,numeric_value,raw_text) "
        "values($1::uuid,'宽度',0.2,'0.2mm')", observation_2026_);
    const auto changed = repository.delete_year(year_v2_, preview->impact_token(), "永久删除 2026", "误建年度", actor());
    EXPECT_EQ(changed.status, bridge_report::deletion::DeleteInspectionYearStatus::ImpactChanged);
}

}  // namespace

// 全系统统一的行锁顺序是 import_records -> inspection_years：所有写路径（保存校对
// 草稿、年度确认、构件绑定、Word 导入）都按这个顺序加锁。删除路径反着来的话两边能凑成
// 循环等待，PostgreSQL 中止其中一个，表现为偶发的保存或删除失败。
//
// 顺序本身只在交错的中间态可观测，所以这条真的把删除跑在另一个线程里，趁它被挡住时
// 从第三条连接探一下年度行**此刻**在不在它手上：
//   - 顺序正确：它卡在 import_records 上，还没碰年度行 -> NOWAIT 探测拿得到锁；
//   - 顺序反了：它先锁了年度行、再卡在 import_records 上 -> NOWAIT 报 55P03。
// 判据是"锁得到/锁不到"，不是等多久，所以不赌时序。
TEST_F(InspectionYearDeletionRepositoryTest, DeletionLocksImportRecordsBeforeTheYear) {
    bridge_report::db::InspectionYearDeletionRepository repository(client_);
    const auto preview = repository.preview(year_v1_);
    ASSERT_TRUE(preview.has_value());
    const auto token = preview->impact_token();

    // 阻塞方握住导入记录——那是写路径的第一把锁，也应当是删除路径的第一把。
    auto blocker_client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);
    auto blocker = blocker_client->newTransaction();
    blocker->execSqlSync(
        "select id from import_records where id=$1::uuid for update", import_id_);

    auto deleter_client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);
    deleter_client->execSqlSync("set statement_timeout = '4000'");
    std::thread deleter([&] {
        bridge_report::db::InspectionYearDeletionRepository(deleter_client)
            .delete_year(year_v1_, token, "永久删除 2026", "锁顺序测试", actor());
    });

    // 等到删除线程确实被锁挡住为止，不用固定 sleep。
    auto probe_client = bridge_report::db::create_db_client(
        bridge_report::config::PostgresConfig{}, 1);
    bool deleter_blocked = false;
    for (int attempt = 0; attempt < 200 && !deleter_blocked; ++attempt) {
        deleter_blocked = probe_client->execSqlSync(
            "select count(*) as n from pg_stat_activity "
            "where wait_event_type='Lock' and datname=current_database()"
        )[0]["n"].as<int>() > 0;
        if (!deleter_blocked) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(deleter_blocked) << "删除线程没有被导入记录的行锁挡住，本条的前提不成立";

    bool year_row_free = true;
    try {
        auto probe = probe_client->newTransaction();
        probe->execSqlSync(
            "select id from inspection_years where id=$1::uuid for update nowait", year_v1_);
        probe->rollback();
    } catch (const drogon::orm::DrogonDbException&) {
        year_row_free = false;  // 55P03：年度行已经在删除线程手上
    }

    blocker->rollback();
    deleter.join();
    blocker_client->closeAll();
    deleter_client->closeAll();
    probe_client->closeAll();

    EXPECT_TRUE(year_row_free)
        << "删除路径在拿到 import_records 之前就锁了 inspection_years，"
           "与所有写路径的顺序相反，两者可凑成死锁";
}

namespace {

// 造一条评定运行。run_kind='试算' 的可删；'正式'+'成功' 由
// protect_completed_formal_assessment_run 保护为不可删。
std::string insert_assessment_run(
    const drogon::orm::DbClientPtr& client,
    const std::string& inspection_year_id,
    const std::string& user_id,
    const std::string& run_kind,
    const std::string& result_status
) {
    // 三个 *_summary_json 都有"必须是非空对象"的检查约束；"正式+成功"另有
    // confirmation_check 要求确认人与确认时间齐全。
    const bool immutable = run_kind == "正式" && result_status == "成功";
    return client->execSqlSync(
        "insert into assessment_runs(inspection_year_id,run_kind,result_status,input_summary_json,"
        "input_checksum,rule_package_summary_json,rule_package_checksum,result_summary_json,"
        "created_by_user_id,formal_revision_number,confirmed_by_user_id,confirmed_at) "
        "values($1::uuid,$2,$3,'{\"source\":\"deletion-test\"}'::jsonb,$4,"
        "'{\"package\":\"deletion-test\"}'::jsonb,$5,'{\"score\":80}'::jsonb,$6::uuid,"
        "case when $7 then 1 else null end,"
        "case when $7 then $6::uuid else null end,"
        "case when $7 then now() else null end) "
        "returning id::text as id",
        inspection_year_id, run_kind, result_status,
        "sha256:" + std::string(64, '1'), "sha256:" + std::string(64, '2'), user_id, immutable
    )[0]["id"].as<std::string>();
}

}  // namespace

// assessment_runs.inspection_year_id 是 on delete restrict：不先删它就删不掉年度。
// 这段此前完全缺失，任何做过评定（哪怕只是试算）的年度都删不掉——而两条删除路径的
// 测试夹具从来不建评定运行，所以测试一直全绿。百股大桥就是被这个卡住的。
TEST_F(InspectionYearDeletionRepositoryTest, DeletesTheYearsDisposableAssessmentRuns) {
    const auto run_id = insert_assessment_run(client_, year_v1_, user_id_, "试算", "成功");

    bridge_report::db::InspectionYearDeletionRepository repository(client_);
    const auto preview = repository.preview(year_v1_);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->counts.assessment_runs, 1) << "影响清单必须把评定运行算进去";
    EXPECT_EQ(preview->counts.formal_assessment_runs, 0);

    const auto outcome = repository.delete_year(
        year_v1_, preview->impact_token(), "永久删除 2026", "评定运行清理", actor());

    ASSERT_EQ(outcome.status, bridge_report::deletion::DeleteInspectionYearStatus::Deleted);
    EXPECT_TRUE(client_->execSqlSync(
        "select 1 from assessment_runs where id=$1::uuid", run_id).empty());
}

// 正式评定是不可变的业务记录，不能被年度删除顺手抹掉。必须在预检就判出来并明确拒绝，
// 而不是让用户点下去撞保护触发器、拿一句看不懂的"删除失败"。
TEST_F(InspectionYearDeletionRepositoryTest, RefusesToDeleteAYearWithACompletedFormalAssessment) {
    // "正式"评定按 assessment_runs_kind_context_check 必须带齐规范上下文与台账版本。
    const auto make_package = [&](const char* tag, const char* family) {
        return client_->execSqlSync(
            "insert into standard_packages(standard_family,standard_id,standard_code,standard_name,"
            "official_edition,package_version,contract_version,algorithm_id,effective_date,content_checksum) "
            "values($1,$2||'-'||gen_random_uuid()::text,$2,'删除测试规范','2026','1.0.0',1,$2,"
            "'2026-01-01','sha256:'||repeat('d',64)) returning id::text as id",
            family, tag)[0]["id"].as<std::string>();
    };
    const auto technical_package = make_package("DEL-TECH", "technical_condition");
    const auto maintenance_package = make_package("DEL-MAINT", "maintenance");
    // 迁移 019：新 profile 必须绑一份已发布评定树，且该年度的病害观测必须挂到它的可选节点上。
    const auto tree_version_id = client_->execSqlSync(
        "insert into rating_tree_versions("
        "tree_code,tree_name,package_version,contract_version,"
        "technical_condition_package_id,technical_condition_standard_id,"
        "technical_condition_package_version,technical_condition_content_checksum,"
        "maintenance_package_id,maintenance_standard_id,"
        "maintenance_package_version,maintenance_content_checksum,"
        "organization_tree_code,organization_package_version,"
        "organization_content_checksum,tree_content_checksum,status"
        ") select 'DEL-TREE-'||gen_random_uuid()::text,'删除测试评定树','1.0.0',1,"
        "       $1::uuid,t.standard_id,t.package_version,t.content_checksum,"
        "       $2::uuid,m.standard_id,m.package_version,m.content_checksum,"
        "       'DEL-TREE-ORG','1.0.0',"
        "       'sha256:'||md5(gen_random_uuid()::text)||md5(gen_random_uuid()::text),"
        "       'sha256:'||md5(gen_random_uuid()::text)||md5(gen_random_uuid()::text),'draft' "
        "from standard_packages t, standard_packages m "
        "where t.id=$1::uuid and m.id=$2::uuid returning id::text as id",
        technical_package, maintenance_package)[0]["id"].as<std::string>();
    client_->execSqlSync(
        "insert into rating_tree_nodes("
        "rating_tree_version_id,node_key,display_name,node_type,scoring_mode,is_selectable"
        ") values ($1::uuid,'root','桥梁评定','root','non_scoring',false),"
        "         ($1::uuid,'defect.test','裂缝','defect','non_scoring',true)",
        tree_version_id);
    // published 要求 published_at 非空（018 的 publish_state_check），因此分两步。
    client_->execSqlSync(
        "update rating_tree_versions set status='published',published_at=now() where id=$1::uuid",
        tree_version_id);
    const auto defect_node_id = client_->execSqlSync(
        "select id::text as id from rating_tree_nodes "
        "where rating_tree_version_id=$1::uuid and node_key='defect.test'",
        tree_version_id)[0]["id"].as<std::string>();
    const auto profile_id = client_->execSqlSync(
        "insert into project_standard_profiles(technical_condition_package_id,maintenance_package_id,"
        "rating_tree_version_id,created_by_user_id,change_reason) "
        "values($1::uuid,$2::uuid,$4::uuid,$3::uuid,'删除测试') "
        "returning id::text as id",
        technical_package, maintenance_package, user_id_,
        tree_version_id)[0]["id"].as<std::string>();
    const auto revision_id = client_->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,created_by_user_id) "
        "values($1::uuid,1,$2::uuid) returning id::text as id",
        bridge_id_, user_id_)[0]["id"].as<std::string>();
    // validate_assessment_run_context() 还要求台账版本已确认。
    client_->execSqlSync(
        "update bridge_component_inventory_revisions set status='已确认',"
        "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
        revision_id, user_id_);

    const auto run_id = client_->execSqlSync(
        "insert into assessment_runs(inspection_year_id,run_kind,result_status,input_summary_json,"
        "input_checksum,rule_package_summary_json,rule_package_checksum,result_summary_json,"
        "created_by_user_id,formal_revision_number,technical_condition_package_id,standard_profile_id,"
        // 迁移 019 的身份触发器：run 的评定树版本与校验和必须等于 profile 所绑的那一份。
        "component_inventory_revision_id,is_current,confirmed_by_user_id,confirmed_at,"
        "rating_tree_version_id,rating_tree_content_checksum) "
        "values($1::uuid,'正式','成功','{\"source\":\"deletion-test\"}'::jsonb,$2,"
        "'{\"package\":\"deletion-test\"}'::jsonb,$3,'{\"score\":80}'::jsonb,$4::uuid,1,"
        "$5::uuid,$6::uuid,$7::uuid,true,$4::uuid,now(),"
        "(select rating_tree_version_id from project_standard_profiles where id=$6::uuid),"
        "(select v.tree_content_checksum from rating_tree_versions v "
        " join project_standard_profiles p on p.rating_tree_version_id=v.id where p.id=$6::uuid)) "
        "returning id::text as id",
        year_v1_, "sha256:" + std::string(64, '3'),
        // validate_assessment_run_context() 要求它等于锁定规范包的 content_checksum。
        "sha256:" + std::string(64, 'd'),
        user_id_, technical_package, profile_id, revision_id)[0]["id"].as<std::string>();

    bridge_report::db::InspectionYearDeletionRepository repository(client_);
    const auto preview = repository.preview(year_v1_);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->counts.formal_assessment_runs, 1);
    EXPECT_EQ(preview->counts.assessment_runs, 0) << "不可删的那条不该算进可删计数";

    const auto outcome = repository.delete_year(
        year_v1_, preview->impact_token(), "永久删除 2026", "不该成功", actor());

    EXPECT_EQ(outcome.status,
              bridge_report::deletion::DeleteInspectionYearStatus::FormalAssessmentPresent);
    EXPECT_FALSE(client_->execSqlSync(
        "select 1 from assessment_runs where id=$1::uuid", run_id).empty())
        << "拒绝之后正式评定必须原样还在";
    EXPECT_FALSE(client_->execSqlSync(
        "select 1 from inspection_years where id=$1::uuid", year_v1_).empty())
        << "年度也必须原样还在";

    // 本条自建的规范组合/规范包不在夹具的清理范围内；project_standard_profiles
    // 对 users 是 on delete restrict，留着会让 TearDown 删不掉测试用户。
    client_->execSqlSync(
        "alter table assessment_runs disable trigger trg_assessment_runs_completed_formal_immutable");
    client_->execSqlSync("delete from assessment_runs where id=$1::uuid", run_id);
    client_->execSqlSync(
        "alter table assessment_runs enable trigger trg_assessment_runs_completed_formal_immutable");
    client_->execSqlSync("delete from project_standard_profiles where id=$1::uuid", profile_id);
    // 已发布的评定树不可变（018 的 protect_published_rating_tree_version），清场时暂停它；
    // 而评定树版本对规范包是 RESTRICT 外键，所以它必须排在规范包之前删。
    client_->execSqlSync(
        "alter table rating_tree_versions disable trigger "
        "trg_rating_tree_versions_published_immutable");
    client_->execSqlSync("delete from rating_tree_versions where id=$1::uuid", tree_version_id);
    client_->execSqlSync(
        "alter table rating_tree_versions enable trigger "
        "trg_rating_tree_versions_published_immutable");
    client_->execSqlSync("delete from standard_packages where id=$1::uuid", technical_package);
    client_->execSqlSync("delete from standard_packages where id=$1::uuid", maintenance_package);
    // 夹具的 TearDown 先删用户，任何 on delete restrict 引用它的行都会把删除挡住。
    client_->execSqlSync(
        "delete from bridge_component_inventory_revisions where id=$1::uuid", revision_id);
}
