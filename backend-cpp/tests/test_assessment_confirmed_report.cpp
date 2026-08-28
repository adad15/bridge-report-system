#include <cstdlib>
#include <string>
#include <utility>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/assessment/AssessmentService.hpp"
#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"

namespace assessment = bridge_report::assessment;

namespace {

// 已入库评定的只读读取夹具。一座桥两个年度，各挂一条导入记录：
//   * 2024：本次要读的那条记录，先落一次试算（必须被忽略），再落正式评定
//   * 2023：另一条记录的正式评定，用来证明不会串到别人的分数上
//   * 第三条记录挂在 2024 上但从未入库，用来验"没有结果"与"记录不存在"分得开
class ConfirmedAssessmentReadTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        const bridge_report::config::PostgresConfig config{};
        client_ = bridge_report::db::create_db_client(config, 1);

        bridge_id_ = insert_id("insert into bridges(bridge_name) values('评定回执测试桥') returning id");
        user_id_ = insert_id(
            "insert into users(username,display_name,password_hash,role) "
            "values($1,'评定回执测试员','not-a-real-hash','normal') returning id",
            "confirmed_assessment_" + bridge_id_);
        technical_package_id_ = insert_id(
            "insert into standard_packages(standard_family,standard_id,standard_code,standard_name,"
            "official_edition,package_version,contract_version,algorithm_id,effective_date,content_checksum) "
            "values('technical_condition',$1,'TEST H21','回执测试技术标准','2026','1.0.3',1,"
            "'test-h21','2026-01-01',$2) returning id",
            "REPORT-TECH-" + bridge_id_, package_checksum_);
        const auto maintenance_package_id = insert_id(
            "insert into standard_packages(standard_family,standard_id,standard_code,standard_name,"
            "official_edition,package_version,contract_version,algorithm_id,effective_date,content_checksum) "
            "values('maintenance',$1,'TEST 5120','回执测试养护规范','2026','1.0.0',1,"
            "'test-maintenance','2026-01-01',$2) returning id",
            "REPORT-MAINT-" + bridge_id_, "sha256:" + std::string(64, 'd'));
        // 迁移 019：新 profile 必须绑一份已发布评定树。校验和列唯一，因此当场生成。
        rating_tree_version_id_ = insert_id(
            "insert into rating_tree_versions("
            "tree_code,tree_name,package_version,contract_version,"
            "technical_condition_package_id,technical_condition_standard_id,"
            "technical_condition_package_version,technical_condition_content_checksum,"
            "maintenance_package_id,maintenance_standard_id,"
            "maintenance_package_version,maintenance_content_checksum,"
            "organization_tree_code,organization_package_version,"
            "organization_content_checksum,tree_content_checksum,status"
            ") select $1,'回执测试评定树','1.0.0',1,$2::uuid,t.standard_id,t.package_version,t.content_checksum,"
            "       $3::uuid,m.standard_id,m.package_version,m.content_checksum,$1,'1.0.0',"
            "       'sha256:'||md5(gen_random_uuid()::text)||md5(gen_random_uuid()::text),"
            "       'sha256:'||md5(gen_random_uuid()::text)||md5(gen_random_uuid()::text),'draft' "
            "from standard_packages t, standard_packages m "
            "where t.id=$2::uuid and m.id=$3::uuid returning id",
            "REPORT-TREE-" + bridge_id_, technical_package_id_, maintenance_package_id);
        client_->execSqlSync(
            "insert into rating_tree_nodes("
            "rating_tree_version_id,node_key,display_name,node_type,scoring_mode,is_selectable"
            ") values ($1::uuid,'root','桥梁评定','root','non_scoring',false),"
            "         ($1::uuid,'defect.test','裂缝','defect','non_scoring',true)",
            rating_tree_version_id_);
        // published 要求 published_at 非空（018 的 publish_state_check），因此分两步。
        client_->execSqlSync(
            "update rating_tree_versions set status='published',published_at=now() where id=$1::uuid",
            rating_tree_version_id_);
        profile_id_ = insert_id(
            "insert into project_standard_profiles(technical_condition_package_id,maintenance_package_id,"
            "rating_tree_version_id,created_by_user_id,change_reason) "
            "values($1::uuid,$2::uuid,$4::uuid,$3::uuid,'评定回执测试') returning id",
            technical_package_id_, maintenance_package_id, user_id_, rating_tree_version_id_);
        inventory_revision_id_ = insert_id(
            "insert into bridge_component_inventory_revisions(bridge_id,revision_number,created_by_user_id) "
            "values($1::uuid,1,$2::uuid) returning id",
            bridge_id_, user_id_);
        client_->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认',"
            "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
            inventory_revision_id_, user_id_);

        year_2024_ = insert_year(2024);
        year_2023_ = insert_year(2023);
        confirmed_record_ = insert_record(year_2024_, "已入库.docx", "已确认");
        other_record_ = insert_record(year_2023_, "别人家的.docx", "已确认");
        pending_record_ = insert_record(year_2024_, "还在校对.docx", "待校对");

        // 试算与正式同挂一条记录：读取必须只认正式那条。
        insert_trial_run(year_2024_, confirmed_record_, R"({"result":{"overall_score":11.0}})");
        confirmed_run_ = insert_formal_run(
            year_2024_, confirmed_record_, 1,
            R"({"result":{"overall_score":83.25,"final_grade":2},"issues":[]})");
        insert_formal_run(year_2023_, other_record_, 1,
                          R"({"result":{"overall_score":42.5},"issues":[]})");
    }

    void TearDown() override {
        if (client_ == nullptr || bridge_id_.empty()) return;
        // 正式且成功的运行受不可变触发器保护，清理夹具时要先关掉它。
        client_->execSqlSync(
            "alter table assessment_runs disable trigger trg_assessment_runs_completed_formal_immutable");
        client_->execSqlSync(
            "delete from assessment_runs where inspection_year_id in "
            "(select id from inspection_years where bridge_id=$1::uuid)",
            bridge_id_);
        client_->execSqlSync(
            "alter table assessment_runs enable trigger trg_assessment_runs_completed_formal_immutable");
        client_->execSqlSync("delete from import_records where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync(
            "delete from bridge_component_inventory_revisions where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from project_standard_profiles where id=$1::uuid", profile_id_);
        // 已发布的评定树不可变（018 的 protect_published_rating_tree_version），清场时暂停它；
        // 而评定树版本对规范包是 RESTRICT 外键，所以它必须排在规范包之前删。
        client_->execSqlSync(
            "alter table rating_tree_versions disable trigger "
            "trg_rating_tree_versions_published_immutable");
        client_->execSqlSync(
            "delete from rating_tree_versions where id=$1::uuid", rating_tree_version_id_);
        client_->execSqlSync(
            "alter table rating_tree_versions enable trigger "
            "trg_rating_tree_versions_published_immutable");
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from users where id=$1::uuid", user_id_);
    }

    template <typename... Args>
    std::string insert_id(const std::string& sql, Args&&... args) {
        return client_->execSqlSync(sql, std::forward<Args>(args)...)[0]["id"]
            .template as<std::string>();
    }

    std::string insert_year(int year) {
        return insert_id(
            "insert into inspection_years(bridge_id,inspection_year,status,version_number,is_current,"
            "standard_profile_id,component_inventory_revision_id) "
            "values($1::uuid,$2,'已确认',1,true,$3::uuid,$4::uuid) returning id",
            bridge_id_, year, profile_id_, inventory_revision_id_);
    }

    std::string insert_record(const std::string& year_id, const std::string& name,
                              const std::string& status) {
        return insert_id(
            "insert into import_records(bridge_id,inspection_year_id,import_name,source_type,import_status) "
            "values($1::uuid,$2::uuid,$3,'正式Word',$4) returning id",
            bridge_id_, year_id, name, status);
    }

    // 正式运行落库分两步：'运行中' 插入，再更新成 '成功'——完成态受触发器保护改不动。
    std::string insert_formal_run(const std::string& year_id, const std::string& record_id,
                                  int revision, const std::string& summary) {
        std::string identity = standard_identity_;
        std::string checksum = package_checksum_;
        const auto run_id = insert_id(
            "insert into assessment_runs(inspection_year_id,source_import_record_id,run_kind,result_status,"
            "input_summary_json,input_checksum,rule_package_summary_json,rule_package_checksum,"
            "result_summary_json,created_by_user_id,formal_revision_number,"
            // 迁移 019 的身份触发器：run 的评定树版本与校验和必须等于 profile 所绑的那一份。
            "technical_condition_package_id,standard_profile_id,component_inventory_revision_id,"
            "rating_tree_version_id,rating_tree_content_checksum) "
            "values($1::uuid,$2::uuid,'正式','运行中','{\"source\":\"report-test\"}'::jsonb,$3,"
            "$4::jsonb,$5,'{}'::jsonb,$6::uuid,$7,$8::uuid,$9::uuid,$10::uuid,"
            "(select rating_tree_version_id from project_standard_profiles where id=$9::uuid),"
            "(select v.tree_content_checksum from rating_tree_versions v "
            " join project_standard_profiles p on p.rating_tree_version_id=v.id where p.id=$9::uuid)) "
            "returning id",
            year_id, record_id, "sha256:" + std::string(64, 'a'), identity, checksum,
            user_id_, revision, technical_package_id_, profile_id_, inventory_revision_id_);
        std::string body = summary;
        client_->execSqlSync(
            "update assessment_runs set result_status='成功',is_current=true,"
            "confirmed_by_user_id=$2::uuid,confirmed_at=now(),result_summary_json=$3::jsonb "
            "where id=$1::uuid",
            run_id, user_id_, body);
        return run_id;
    }

    std::string insert_trial_run(const std::string& year_id, const std::string& record_id,
                                 const std::string& summary) {
        std::string body = summary;
        return insert_id(
            "insert into assessment_runs(inspection_year_id,source_import_record_id,run_kind,result_status,"
            "input_summary_json,input_checksum,rule_package_summary_json,rule_package_checksum,"
            "result_summary_json,created_by_user_id) "
            "values($1::uuid,$2::uuid,'试算','成功','{\"source\":\"report-test\"}'::jsonb,$3,"
            "'{\"package\":\"trial\"}'::jsonb,$4,$5::jsonb,$6::uuid) returning id",
            year_id, record_id, "sha256:" + std::string(64, 'a'),
            "sha256:" + std::string(64, 'b'), body, user_id_);
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string user_id_;
    std::string technical_package_id_;
    std::string profile_id_;
    std::string rating_tree_version_id_;
    std::string inventory_revision_id_;
    std::string year_2024_;
    std::string year_2023_;
    std::string confirmed_record_;
    std::string other_record_;
    std::string pending_record_;
    std::string confirmed_run_;
    std::string package_checksum_ = "sha256:" + std::string(64, 'c');
    std::string standard_identity_ =
        R"({"standard_code":"TEST H21","standard_name":"回执测试技术标准","package_version":"1.0.3"})";
};

TEST_F(ConfirmedAssessmentReadTest, ReturnsTheFormalRunThisRecordWrote) {
    const auto outcome = assessment::fetch_confirmed_assessment(client_, confirmed_record_);

    ASSERT_EQ(outcome.status, assessment::ConfirmedAssessmentStatus::Ok);
    EXPECT_EQ(outcome.report.assessment_run_id, confirmed_run_)
        << "同一条记录上还挂着一次试算，读取必须只认正式那条";
    EXPECT_DOUBLE_EQ(outcome.report.result["overall_score"].asDouble(), 83.25);
    EXPECT_EQ(outcome.report.standard_identity["package_version"].asString(), "1.0.3");
    EXPECT_EQ(outcome.report.inspection_year, 2024);
    EXPECT_EQ(outcome.report.inspection_year_version, 1);
    EXPECT_EQ(outcome.report.formal_revision_number, 1);
    EXPECT_TRUE(outcome.report.is_current);
    EXPECT_TRUE(outcome.report.inspection_year_is_current);
    EXPECT_FALSE(outcome.report.confirmed_at.empty());
}

// 一座桥上多条记录各自入过库；读谁的记录就该拿谁的分数，不能被另一条顶替。
TEST_F(ConfirmedAssessmentReadTest, KeepsEachRecordOnItsOwnRun) {
    const auto outcome = assessment::fetch_confirmed_assessment(client_, other_record_);

    ASSERT_EQ(outcome.status, assessment::ConfirmedAssessmentStatus::Ok);
    EXPECT_DOUBLE_EQ(outcome.report.result["overall_score"].asDouble(), 42.5);
    EXPECT_EQ(outcome.report.inspection_year, 2023);
}

// 年度被修订成 v2 之后，旧记录该显示的仍是它当年入库的那一份，但要能看出已被顶替。
TEST_F(ConfirmedAssessmentReadTest, MarksTheReportOfASupersededYear) {
    client_->execSqlSync(
        "update inspection_years set is_current=false,status='已被修订' where id=$1::uuid",
        year_2024_);

    const auto outcome = assessment::fetch_confirmed_assessment(client_, confirmed_record_);

    ASSERT_EQ(outcome.status, assessment::ConfirmedAssessmentStatus::Ok);
    EXPECT_DOUBLE_EQ(outcome.report.result["overall_score"].asDouble(), 83.25)
        << "旧记录仍要读得到自己当年的分数";
    EXPECT_FALSE(outcome.report.inspection_year_is_current);
    EXPECT_TRUE(outcome.report.is_current) << "运行本身没被同年度的另一次评定顶替";
}

TEST_F(ConfirmedAssessmentReadTest, SeparatesNeverConfirmedFromMissingRecord) {
    EXPECT_EQ(assessment::fetch_confirmed_assessment(client_, pending_record_).status,
              assessment::ConfirmedAssessmentStatus::ReportNotFound);
    EXPECT_EQ(assessment::fetch_confirmed_assessment(
                  client_, "00000000-0000-0000-0000-000000000000").status,
              assessment::ConfirmedAssessmentStatus::ImportRecordNotFound);
}

}  // namespace
