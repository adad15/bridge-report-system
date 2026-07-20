#include <cstdlib>
#include <string>

#include <gtest/gtest.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ImportRecordDeletionRepository.hpp"

namespace {

class ImportRecordDeletionRepositoryTest : public testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL is not set";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{}, 1);
        bridge_id_ = client_->execSqlSync(
            "insert into bridges(bridge_name) values('导入记录删除仓储测试桥') returning id::text as id"
        )[0]["id"].as<std::string>();
        year_id_ = client_->execSqlSync(
            "insert into inspection_years(bridge_id,inspection_year,status,is_current) "
            "values($1::uuid,2026,'待校对',true) returning id::text as id",
            bridge_id_
        )[0]["id"].as<std::string>();
        import_id_ = client_->execSqlSync(
            "insert into import_records(bridge_id,inspection_year_id,import_name,source_type,import_status,parsed_result_json) "
            "values($1::uuid,$2::uuid,'待删除报告.docx','软件导出Word','待校对',"
            "'{\"defects\":[{},{}],\"photos\":[{}],\"ratings\":{\"overall\":{},\"structure_parts\":[{}]}}'::jsonb) "
            "returning id::text as id",
            bridge_id_, year_id_
        )[0]["id"].as<std::string>();
        other_import_id_ = client_->execSqlSync(
            "insert into import_records(bridge_id,inspection_year_id,import_name,source_type,import_status) "
            "values($1::uuid,$2::uuid,'保留共享文件的报告.docx','软件导出Word','待校对') returning id::text as id",
            bridge_id_, year_id_
        )[0]["id"].as<std::string>();

        exclusive_file_id_ = insert_archived_file("imports/exclusive.jpg", "独占照片.jpg");
        shared_file_id_ = insert_archived_file("imports/shared.jpg", "共享照片.jpg");
        client_->execSqlSync(
            "update import_records set main_file_id=$2::uuid where id=$1::uuid",
            import_id_, exclusive_file_id_
        );
        client_->execSqlSync(
            "insert into import_record_files(import_record_id,archived_file_id,file_role) "
            "values($1::uuid,$2::uuid,'附件'),($3::uuid,$2::uuid,'附件')",
            import_id_, shared_file_id_, other_import_id_
        );
        source_file_id_ = client_->execSqlSync(
            "insert into import_source_files(import_record_id,original_file_name,storage_relative_path,"
            "file_extension,file_size_bytes,file_hash,status,active_parse_work_relative_path) "
            "values($1::uuid,'待删除报告.docx',$2,'.docx',8,$3,'待解析',$4) returning id::text as id",
            import_id_, import_id_ + ".docx", std::string(64, 'e'),
            "work/word-import/" + import_id_ + "-abcdef12"
        )[0]["id"].as<std::string>();

        const auto suffix = import_id_.substr(0, 8);
        user_id_ = client_->execSqlSync(
            "insert into users(username,display_name,password_hash,role) "
            "values($1,$2,'test-password-hash','admin') returning id::text as id",
            "delete-import-" + suffix, "删除测试管理员"
        )[0]["id"].as<std::string>();
        session_id_ = client_->execSqlSync(
            "insert into user_sessions(user_id,token_hash,expires_at) "
            "values($1::uuid,$2,now()+interval '1 hour') returning id::text as id",
            user_id_, std::string(56, 's') + suffix
        )[0]["id"].as<std::string>();
    }

    void TearDown() override {
        if (!client_) return;
        client_->execSqlSync(
            "delete from import_record_deletion_audits where original_import_record_id=$1::uuid",
            import_id_
        );
        client_->execSqlSync("delete from import_record_edit_locks where import_record_id=$1::uuid", import_id_);
        client_->execSqlSync("delete from defect_observations where source_import_record_id=$1::uuid", import_id_);
        client_->execSqlSync("delete from import_source_files where id=$1::uuid", source_file_id_);
        client_->execSqlSync("delete from import_records where id=$1::uuid", import_id_);
        client_->execSqlSync("delete from import_records where id=$1::uuid", other_import_id_);
        client_->execSqlSync("delete from archived_files where id=$1::uuid", exclusive_file_id_);
        client_->execSqlSync("delete from archived_files where id=$1::uuid", shared_file_id_);
        client_->execSqlSync("delete from bridge_components where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from user_sessions where id=$1::uuid", session_id_);
        client_->execSqlSync("delete from users where id=$1::uuid", user_id_);
        client_->execSqlSync("delete from inspection_years where id=$1::uuid", year_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        client_->closeAll();
    }

    std::string insert_archived_file(const std::string& path, const std::string& name) {
        return client_->execSqlSync(
            "insert into archived_files(bridge_id,inspection_year_id,original_file_name,current_file_name,"
            "storage_relative_path,file_type,file_purpose) "
            "values($1::uuid,$2::uuid,$3,$3,$4,'图片','删除仓储测试') returning id::text as id",
            bridge_id_, year_id_, name, path
        )[0]["id"].as<std::string>();
    }

    bridge_report::deletion::DeletionActorSnapshot actor() const {
        return {user_id_, "delete-import-test", "删除测试管理员"};
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string year_id_;
    std::string import_id_;
    std::string other_import_id_;
    std::string exclusive_file_id_;
    std::string shared_file_id_;
    std::string source_file_id_;
    std::string user_id_;
    std::string session_id_;
};

TEST_F(ImportRecordDeletionRepositoryTest, PreviewCountsBusinessDataAndRetainsSharedFiles) {
    bridge_report::db::ImportRecordDeletionRepository repository(client_);

    const auto plan = repository.preview(import_id_);

    ASSERT_TRUE(plan.has_value());
    EXPECT_TRUE(plan->can_delete());
    EXPECT_EQ(plan->counts.defects, 2);
    EXPECT_EQ(plan->counts.photos, 1);
    EXPECT_EQ(plan->counts.rating_items, 0);
    EXPECT_EQ(plan->counts.archived_files_to_delete, 1);
    EXPECT_EQ(plan->counts.shared_files_retained, 1);
    EXPECT_EQ(plan->counts.temporary_word_files_to_delete, 1);
    EXPECT_EQ(plan->counts.parse_work_directories_to_delete, 1);
    EXPECT_EQ(plan->confirmation_text(), "永久删除 " + plan->import_system_number);
}

TEST_F(ImportRecordDeletionRepositoryTest, ActiveEditLockBlocksDeletion) {
    client_->execSqlSync(
        "insert into import_record_edit_locks(import_record_id,user_id,user_session_id,lock_token_hash,expires_at) "
        "values($1::uuid,$2::uuid,$3::uuid,$4,now()+interval '10 minutes')",
        import_id_, user_id_, session_id_, std::string(64, 'l')
    );
    bridge_report::db::ImportRecordDeletionRepository repository(client_);
    const auto plan = repository.preview(import_id_);
    ASSERT_TRUE(plan.has_value());

    const auto outcome = repository.delete_import_record(import_id_, plan->impact_token(), "锁定测试", actor());

    EXPECT_EQ(outcome.status, bridge_report::deletion::DeleteImportRecordStatus::Locked);
    EXPECT_FALSE(client_->execSqlSync("select 1 from import_records where id=$1::uuid", import_id_).empty());
}

TEST_F(ImportRecordDeletionRepositoryTest, FormalFactsBlockDeletion) {
    const auto component_id = client_->execSqlSync(
        "insert into bridge_components(bridge_id,structure_part,component_type,business_component_code,normalized_component_key) "
        "values($1::uuid,'上部结构','上部承重构件','2-1#板',$2) returning id::text as id",
        bridge_id_, "delete-test-" + import_id_
    )[0]["id"].as<std::string>();
    client_->execSqlSync(
        "insert into defect_observations(inspection_year_id,bridge_id,bridge_component_id,source_import_record_id,"
        "structure_part,defect_type,defect_description_raw) "
        "values($1::uuid,$2::uuid,$3::uuid,$4::uuid,'上部结构','蜂窝麻面','正式事实测试')",
        year_id_, bridge_id_, component_id, import_id_
    );
    bridge_report::db::ImportRecordDeletionRepository repository(client_);
    const auto plan = repository.preview(import_id_);
    ASSERT_TRUE(plan.has_value());

    const auto outcome = repository.delete_import_record(import_id_, plan->impact_token(), "正式事实测试", actor());

    EXPECT_EQ(outcome.status, bridge_report::deletion::DeleteImportRecordStatus::HasFormalFacts);
    EXPECT_FALSE(client_->execSqlSync("select 1 from import_records where id=$1::uuid", import_id_).empty());
}

TEST_F(ImportRecordDeletionRepositoryTest, ChangedImpactRejectsStaleConfirmation) {
    bridge_report::db::ImportRecordDeletionRepository repository(client_);
    const auto plan = repository.preview(import_id_);
    ASSERT_TRUE(plan.has_value());
    client_->execSqlSync("update import_records set import_status='已取消' where id=$1::uuid", import_id_);

    const auto outcome = repository.delete_import_record(import_id_, plan->impact_token(), "竞态测试", actor());

    EXPECT_EQ(outcome.status, bridge_report::deletion::DeleteImportRecordStatus::ImpactChanged);
    ASSERT_TRUE(outcome.current_plan.has_value());
    EXPECT_EQ(outcome.current_plan->import_status, "已取消");
}

TEST_F(ImportRecordDeletionRepositoryTest, DeletesRecordAndCreatesAuditCleanupQueue) {
    bridge_report::db::ImportRecordDeletionRepository repository(client_);
    const auto plan = repository.preview(import_id_);
    ASSERT_TRUE(plan.has_value());

    const auto outcome = repository.delete_import_record(import_id_, plan->impact_token(), "误上传测试文件", actor());

    ASSERT_EQ(outcome.status, bridge_report::deletion::DeleteImportRecordStatus::Deleted);
    ASSERT_TRUE(outcome.deletion_audit_id.has_value());
    EXPECT_TRUE(client_->execSqlSync("select 1 from import_records where id=$1::uuid", import_id_).empty());
    EXPECT_TRUE(client_->execSqlSync("select 1 from archived_files where id=$1::uuid", exclusive_file_id_).empty());
    EXPECT_FALSE(client_->execSqlSync("select 1 from archived_files where id=$1::uuid", shared_file_id_).empty());
    const auto audit = client_->execSqlSync(
        "select import_system_number_snapshot,reason,file_cleanup_status from import_record_deletion_audits "
        "where id=$1::uuid",
        *outcome.deletion_audit_id
    );
    ASSERT_EQ(audit.size(), 1u);
    EXPECT_EQ(audit[0]["import_system_number_snapshot"].as<std::string>(), plan->import_system_number);
    EXPECT_EQ(audit[0]["reason"].as<std::string>(), "误上传测试文件");
    EXPECT_EQ(audit[0]["file_cleanup_status"].as<std::string>(), "待清理");
    const auto queue = client_->execSqlSync(
        "select storage_kind,artifact_kind,storage_relative_path from import_record_file_deletion_queue "
        "where import_record_deletion_audit_id=$1::uuid order by storage_kind,artifact_kind",
        *outcome.deletion_audit_id
    );
    ASSERT_EQ(queue.size(), 3u);
}

}  // namespace
