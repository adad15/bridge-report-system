#include <cstdlib>
#include <string>

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
            "insert into condition_ratings(inspection_year_id,rating_level,structure_part,bridge_component_id,rating_item_name,score) values($1::uuid,'构件','上部结构',$2::uuid,'2-1#板',65)", year_v2_, component_id_);
        client_->execSqlSync(
            "insert into defect_comparisons(bridge_id,current_inspection_year_id,compared_inspection_year_id,comparison_result) values($1::uuid,$2::uuid,$3::uuid,'延续')",
            bridge_id_, year_v2_, year_2025_);
        exclusive_file_id_ = archived_file(year_v2_, "delete-tests/exclusive.docx");
        shared_file_id_ = archived_file(year_v2_, "delete-tests/shared.docx");
        client_->execSqlSync("insert into bridge_aliases(bridge_id,alias_name,source_file_id) values($1::uuid,'删除测试别名',$2::uuid)", bridge_id_, shared_file_id_);
        import_id_ = id(
            "insert into import_records(bridge_id,inspection_year_id,import_name,source_type,main_file_id) values($1::uuid,$2::uuid,'删除测试导入','正式Word',$3::uuid) returning id",
            bridge_id_, year_v2_, exclusive_file_id_);
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
    std::string observation_2025_, observation_2026_, exclusive_file_id_, shared_file_id_, import_id_, user_id_;
};

TEST_F(InspectionYearDeletionRepositoryTest, DeletesAllVersionsAndRetainsOtherYearThreadAndSharedFile) {
    bridge_report::db::InspectionYearDeletionRepository repository(client_);
    const auto preview = repository.preview(year_v2_);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->counts.inspection_versions, 2);
    EXPECT_EQ(preview->counts.import_records, 1);
    EXPECT_EQ(preview->counts.defect_observations, 1);
    EXPECT_EQ(preview->counts.defect_measurements, 1);
    EXPECT_EQ(preview->counts.condition_ratings, 1);
    EXPECT_EQ(preview->counts.defect_comparisons, 1);
    EXPECT_EQ(preview->counts.archived_files_to_delete, 1);
    EXPECT_EQ(preview->counts.shared_files_retained, 1);

    const auto outcome = repository.delete_year(year_v1_, preview->impact_token(), "永久删除 2026", "误建年度", actor());
    ASSERT_EQ(outcome.status, bridge_report::deletion::DeleteInspectionYearStatus::Deleted);
    EXPECT_EQ(outcome.next_inspection_year_id, year_2025_);
    EXPECT_TRUE(client_->execSqlSync("select 1 from inspection_years where bridge_id=$1::uuid and inspection_year=2026", bridge_id_).empty());
    EXPECT_FALSE(client_->execSqlSync("select 1 from defect_threads where id=$1::uuid", thread_id_).empty());
    EXPECT_FALSE(client_->execSqlSync("select 1 from archived_files where id=$1::uuid", shared_file_id_).empty());
    EXPECT_TRUE(client_->execSqlSync("select 1 from archived_files where id=$1::uuid", exclusive_file_id_).empty());
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
        "insert into condition_ratings(inspection_year_id,rating_level,structure_part,rating_item_name,score) "
        "values($1::uuid,'全桥','全桥','全桥',80)", year_v2_);
    const auto changed = repository.delete_year(year_v2_, preview->impact_token(), "永久删除 2026", "误建年度", actor());
    EXPECT_EQ(changed.status, bridge_report::deletion::DeleteInspectionYearStatus::ImpactChanged);
}

}  // namespace
