#include <cstdlib>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ReportPreflightRepository.hpp"

namespace {

/// 整个进程共用一个连接：max_connections 是 100，每个用例各开一个会耗尽连接池。
drogon::orm::DbClientPtr shared_test_client() {
    static drogon::orm::DbClientPtr client = [] {
        const bridge_report::config::PostgresConfig config{};
        return bridge_report::db::create_db_client(config, 1);
    }();
    return client;
}

// 报告生成前检查的集成夹具（设计 §16、§10.1、§13）。
//
// 夹具只搭到"病害、构件台账、模板、人员、设备都齐了"这一步，不构造正式评定——
// 评定的前置条件（规范包、评分树、标准配置）自成一套，与这里要验的规则无关。
// 因此基线上永远带着一条 REPORT_ASSESSMENT_REQUIRED，各用例断言的是**自己那一条**
// 是否出现，而不是"全绿"。
class ReportPreflightRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        client_ = shared_test_client();
        tag_ = ::testing::UnitTest::GetInstance()->current_test_info()->name();

        user_id_ = id("insert into users (username, display_name, password_hash, role) "
                      "values ($1, '预检测试员', 'x', 'admin') returning id",
                      "preflight_" + tag_);
        bridge_id_ = id("insert into bridges (bridge_name) values ('预检测试桥') returning id");
        revision_id_ = id(
            "insert into bridge_component_inventory_revisions"
            "(bridge_id, revision_number, status, created_by_user_id) "
            "values ($1::uuid, 1, '草稿', $2::uuid) returning id",
            bridge_id_, user_id_);
        // 用例可能用到的构件全部在这里建齐：修订版一旦确认就彻底不可变，
        // 事后补条目会撞 protect_confirmed_component_inventory_entry。
        component_id_ = add_component("上部结构", "1-1#板");
        add_inventory_entry(component_id_, "1-1#板", true);
        second_component_id_ = add_component("上部结构", "1-2#板");
        add_inventory_entry(second_component_id_, "1-2#板", true);
        inactive_component_id_ = add_component("上部结构", "9-9#板");
        add_inventory_entry(inactive_component_id_, "9-9#板", false);
        deck_component_id_ = add_component("桥面系", "1#跨桥面铺装");
        add_inventory_entry(deck_component_id_, "1#跨桥面铺装", true);
        // 故意不进台账：用于验证"绑定落在锁定修订版之外"这一条。
        stray_component_id_ = add_component("上部结构", "8-8#板");
        confirm_revision();

        // 已确认的年度必须绑定已确认的台账修订版（011 的
        // inspection_year_inventory_revision_confirmed），所以年度最后建。
        year_id_ = id(
            "insert into inspection_years "
            "(bridge_id, inspection_year, status, is_current, component_inventory_revision_id) "
            "values ($1::uuid, 2026, '已确认', true, $2::uuid) returning id",
            bridge_id_, revision_id_);

        file_id_ = id(
            "insert into archived_files (bridge_id, original_file_name, current_file_name, "
            " storage_relative_path, file_type, file_purpose) "
            "values ($1::uuid, 't.docx', 't.docx', $2, '模板', '报告模板') returning id",
            bridge_id_, "templates/preflight-" + tag_ + "/t.docx");
        template_id_ = create_template(true);
        equipment_id_ = id(
            "insert into report_equipment (equipment_name) values ('预检设备') returning id");
        person_id_ = id("insert into report_personnel (full_name) values ('预检签字人') returning id");

        configure(template_id_);
        assign_equipment();
    }

    void TearDown() override {
        if (client_ == nullptr) return;
        client_->execSqlSync("update inspection_years set report_comparison_inspection_id=null "
                             "where bridge_id=$1::uuid", bridge_id_);
        for (const auto& table : {"inspection_report_personnel", "inspection_report_equipment",
                                  "inspection_report_settings", "report_generation_jobs"}) {
            client_->execSqlSync(
                std::string("delete from ") + table +
                " where inspection_year_id in (select id from inspection_years where bridge_id=$1::uuid)",
                bridge_id_);
        }
        client_->execSqlSync("delete from defect_photos where defect_observation_id in "
                             "(select id from defect_observations where bridge_id=$1::uuid)", bridge_id_);
        client_->execSqlSync("delete from defect_observations where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from report_templates where id=$1::uuid", template_id_);
        client_->execSqlSync("delete from report_personnel where id=$1::uuid", person_id_);
        client_->execSqlSync("delete from report_equipment where id=$1::uuid", equipment_id_);
        client_->execSqlSync("delete from inspection_years where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from archived_files where id=$1::uuid", file_id_);
        // 台账修订版、条目、构件、桥梁和建它们的用户都不清理。
        //
        // 已确认的修订版是彻底不可变的：自身挡 UPDATE，条目连 DELETE 都挡（011 的两个
        // 保护触发器），删修订版会级联到条目再被拦一次；而修订版又以 RESTRICT 引用着
        // 创建人，用户也就跟着删不掉。硬要清理只能去关触发器，那会把生产不变量在测试里
        // 打开一个口子。
        //
        // 留着是安全的：这些行都挂在本用例独有的桥上，不与别的测试相干，
        // 而 check-backend-tests.ps1 跑完会把整个隔离 schema drop 掉。
    }

    template <typename... Args>
    std::string id(const std::string& sql, Args&&... args) {
        return client_->execSqlSync(sql, std::forward<Args>(args)...)[0]["id"]
            .template as<std::string>();
    }

    std::string add_component(const std::string& part, const std::string& code) {
        return id("insert into bridge_components(bridge_id, structure_part, component_type, "
                  " business_component_code, normalized_component_key, creation_source) "
                  "values($1::uuid, $2, '构件', $3, $4, '人工录入') returning id",
                  bridge_id_, part, code, "preflight-" + tag_ + "-" + code);
    }

    /// 只能在修订版还是草稿时调用。
    void add_inventory_entry(const std::string& component_id, const std::string& number,
                             bool active) {
        client_->execSqlSync(
            "insert into bridge_component_inventory_entries"
            "(inventory_revision_id, bridge_component_id, component_number, site_name, "
            " site_component_type, is_active, deactivated_at, deactivation_reason) "
            "values ($1::uuid, $2::uuid, $3, '测点', '构件', $4, "
            "        case when $4 then null else now() end, "
            "        case when $4 then null else '测试停用' end)",
            revision_id_, component_id, number, active);
    }

    void confirm_revision() {
        client_->execSqlSync(
            "update bridge_component_inventory_revisions set status='已确认', "
            " confirmed_by_user_id=$2::uuid, confirmed_at=now() where id=$1::uuid",
            revision_id_, user_id_);
    }

    std::string add_defect(const std::string& component_id, const std::string& part,
                           const std::string& source_import = "",
                           const std::string& split_origin = "") {
        return id("insert into defect_observations"
                  "(inspection_year_id, bridge_id, bridge_component_id, structure_part, "
                  " defect_type, defect_description_raw, source_raw_cells_json, source_import_record_id) "
                  "values ($1::uuid, $2::uuid, $3::uuid, $4, '裂缝', '测试病害', "
                  "        coalesce(nullif($5::text,'')::jsonb, '{}'::jsonb), nullif($6::text,'')::uuid) "
                  "returning id",
                  year_id_, bridge_id_, component_id, part, split_origin, source_import);
    }

    std::string create_template(bool enabled, const std::string& anchors_json =
                                    R"(["DEFECT_TABLES:SUPERSTRUCTURE"])",
                                const std::string& roles_json = "[]") {
        return id(
            "insert into report_templates(template_code, template_name, file_id, file_checksum, "
            " contract_config_json, validation_status, validation_result_json, is_enabled, "
            " updated_by_user_id) "
            "values ($1, '预检模板', $2::uuid, $3, "
            "        jsonb_build_object('required_personnel_roles', $4::jsonb), "
            "        'valid', jsonb_build_object('anchors_in_document_order', $5::jsonb), "
            "        $6, $7::uuid) returning id",
            "PF_" + tag_, file_id_, "sha256:" + std::string(64, 'a'), roles_json, anchors_json,
            enabled, user_id_);
    }

    void configure(const std::string& template_id) {
        client_->execSqlSync(
            "insert into inspection_report_settings(inspection_year_id, template_id, "
            " configured_by_user_id, configured_at) values ($1::uuid, $2::uuid, $3::uuid, now()) "
            "on conflict (inspection_year_id) do update set template_id=excluded.template_id",
            year_id_, template_id, user_id_);
    }

    void assign_equipment() {
        client_->execSqlSync(
            "insert into inspection_report_equipment(inspection_year_id, equipment_id) "
            "values ($1::uuid, $2::uuid) on conflict do nothing",
            year_id_, equipment_id_);
    }

    bridge_report::report::ReportPreflightResult evaluate() {
        const auto result =
            bridge_report::db::ReportPreflightRepository(client_).evaluate(year_id_);
        if (!result.has_value()) {
            ADD_FAILURE() << "年度读不回来";
            return {};
        }
        return *result;
    }

    static bool has(const bridge_report::report::ReportPreflightResult& result,
                    const std::string& code) {
        return std::any_of(result.findings.begin(), result.findings.end(),
                           [&](const auto& f) { return f.code == code; });
    }

    drogon::orm::DbClientPtr client_;
    std::string tag_, user_id_, bridge_id_, revision_id_, year_id_, component_id_;
    std::string second_component_id_, inactive_component_id_, deck_component_id_;
    std::string stray_component_id_;
    std::string file_id_, template_id_, equipment_id_, person_id_;
};

TEST_F(ReportPreflightRepositoryTest, UnknownYearReturnsNothing) {
    EXPECT_FALSE(bridge_report::db::ReportPreflightRepository(client_)
                     .evaluate("00000000-0000-0000-0000-000000000000")
                     .has_value());
}

TEST_F(ReportPreflightRepositoryTest, UnconfirmedYearIsBlocked) {
    client_->execSqlSync("update inspection_years set status='待校对' where id=$1::uuid", year_id_);

    EXPECT_TRUE(has(evaluate(), "REPORT_FORMAL_DATA_REQUIRED"));
}

TEST_F(ReportPreflightRepositoryTest, SupersededYearIsBlocked) {
    client_->execSqlSync(
        "update inspection_years set is_current=false, status='已被修订' where id=$1::uuid",
        year_id_);

    EXPECT_TRUE(has(evaluate(), "REPORT_FORMAL_DATA_REQUIRED"));
}

TEST_F(ReportPreflightRepositoryTest, MissingAssessmentIsBlocked) {
    // 夹具不构造正式评定，所以这一条永远在——它同时守着基线的正确性。
    EXPECT_TRUE(has(evaluate(), "REPORT_ASSESSMENT_REQUIRED"));
}

TEST_F(ReportPreflightRepositoryTest, MissingTemplateIsBlocked) {
    client_->execSqlSync("update inspection_report_settings set template_id=null "
                         "where inspection_year_id=$1::uuid", year_id_);

    EXPECT_TRUE(has(evaluate(), "REPORT_TEMPLATE_INVALID"));
}

TEST_F(ReportPreflightRepositoryTest, DisabledTemplateIsBlocked) {
    client_->execSqlSync("update report_templates set is_default=false, is_enabled=false "
                         "where id=$1::uuid", template_id_);

    EXPECT_TRUE(has(evaluate(), "REPORT_TEMPLATE_INVALID"));
}

TEST_F(ReportPreflightRepositoryTest, MissingEquipmentIsBlocked) {
    client_->execSqlSync("delete from inspection_report_equipment where inspection_year_id=$1::uuid",
                         year_id_);

    EXPECT_TRUE(has(evaluate(), "REPORT_EQUIPMENT_REQUIRED"));
}

TEST_F(ReportPreflightRepositoryTest, ConfiguredEquipmentClearsThatCheck) {
    EXPECT_FALSE(has(evaluate(), "REPORT_EQUIPMENT_REQUIRED"));
}

TEST_F(ReportPreflightRepositoryTest, RequiredRoleThatIsNotAssignedIsBlocked) {
    client_->execSqlSync(
        "update report_templates set contract_config_json="
        " jsonb_build_object('required_personnel_roles', '[\"approver\"]'::jsonb) "
        "where id=$1::uuid", template_id_);

    EXPECT_TRUE(has(evaluate(), "REPORT_PERSONNEL_REQUIRED"));
}

TEST_F(ReportPreflightRepositoryTest, AssigningTheRequiredRoleClearsTheCheck) {
    client_->execSqlSync(
        "update report_templates set contract_config_json="
        " jsonb_build_object('required_personnel_roles', '[\"approver\"]'::jsonb) "
        "where id=$1::uuid", template_id_);
    client_->execSqlSync(
        "insert into inspection_report_personnel(inspection_year_id, personnel_id, role_code) "
        "values ($1::uuid, $2::uuid, 'approver')", year_id_, person_id_);

    EXPECT_FALSE(has(evaluate(), "REPORT_PERSONNEL_REQUIRED"));
}

TEST_F(ReportPreflightRepositoryTest, DisabledPersonnelInTheConfigurationIsBlocked) {
    client_->execSqlSync(
        "insert into inspection_report_personnel(inspection_year_id, personnel_id, role_code) "
        "values ($1::uuid, $2::uuid, 'approver')", year_id_, person_id_);
    client_->execSqlSync("update report_personnel set is_enabled=false where id=$1::uuid",
                         person_id_);

    // 停用的人不能被静默沿用（设计 §15.3）。
    EXPECT_TRUE(has(evaluate(), "REPORT_PERSONNEL_REQUIRED"));
}

TEST_F(ReportPreflightRepositoryTest, DefectOnAnInactiveComponentIsBlocked) {
    add_defect(inactive_component_id_, "上部结构");

    EXPECT_TRUE(has(evaluate(), "REPORT_COMPONENT_INACTIVE"));
}

TEST_F(ReportPreflightRepositoryTest, DefectBoundOutsideTheLockedRevisionIsBlocked) {
    // 构件存在，但没有进本年度锁定的台账修订版。
    add_defect(stray_component_id_, "上部结构");

    EXPECT_TRUE(has(evaluate(), "REPORT_FORMAL_DATA_REQUIRED"));
}

TEST_F(ReportPreflightRepositoryTest, StructurePartWithNoTemplateAnchorIsBlocked) {
    // 模板只覆盖上部结构，数据里却有桥面系病害——这些病害无处输出（§16 第 10 条）。
    add_defect(deck_component_id_, "桥面系");

    const auto result = evaluate();
    EXPECT_TRUE(has(result, "REPORT_STRUCTURE_PART_NOT_IN_TEMPLATE"));
    EXPECT_NE(std::find(result.structure_parts_with_data.begin(),
                        result.structure_parts_with_data.end(), "DECK"),
              result.structure_parts_with_data.end());
}

TEST_F(ReportPreflightRepositoryTest, CoveredStructurePartsDoNotTriggerTheCheck) {
    add_defect(component_id_, "上部结构");

    EXPECT_FALSE(has(evaluate(), "REPORT_STRUCTURE_PART_NOT_IN_TEMPLATE"));
}

TEST_F(ReportPreflightRepositoryTest, SourceDefectCountDeduplicatesRangeSplitSiblings) {
    // 同一条来源病害拆成两条观测：共享 source_candidate_id，只该计一次（设计 §12.2）。
    const std::string origin =
        R"({"range_split_origin":{"source_candidate_id":"defect_0001"}})";
    add_defect(component_id_, "上部结构", "", origin);
    add_defect(second_component_id_, "上部结构", "", origin);
    // 另一条未拆分的病害，各计一次。
    add_defect(component_id_, "上部结构");

    const auto result = evaluate();
    EXPECT_EQ(result.defect_observation_count, 3);
    EXPECT_EQ(result.source_defect_count, 2);
}

TEST_F(ReportPreflightRepositoryTest, ComparisonIsRequiredWhenHistoryExists) {
    client_->execSqlSync(
        "insert into inspection_years(bridge_id, inspection_year, status, is_current) "
        "values ($1::uuid, 2025, '已确认', true)", bridge_id_);

    EXPECT_TRUE(has(evaluate(), "REPORT_COMPARISON_REQUIRED"));
}

TEST_F(ReportPreflightRepositoryTest, NoHistoryProducesAWarningNotABlock) {
    const auto result = evaluate();

    const auto finding = std::find_if(result.findings.begin(), result.findings.end(),
        [](const auto& f) { return f.code == "REPORT_COMPARISON_REQUIRED"; });
    ASSERT_NE(finding, result.findings.end());
    // 无历史可比时报告输出固定说明，不该拦住生成（设计 §12.1）。
    EXPECT_EQ(finding->severity, bridge_report::report::PreflightSeverity::Warning);
}

TEST_F(ReportPreflightRepositoryTest, SelectingAValidComparisonClearsTheCheck) {
    const auto previous = id(
        "insert into inspection_years(bridge_id, inspection_year, status, is_current) "
        "values ($1::uuid, 2025, '已确认', true) returning id", bridge_id_);
    client_->execSqlSync(
        "update inspection_years set report_comparison_inspection_id=$2::uuid where id=$1::uuid",
        year_id_, previous);

    const auto result = evaluate();
    EXPECT_FALSE(has(result, "REPORT_COMPARISON_REQUIRED"));
    EXPECT_FALSE(has(result, "REPORT_COMPARISON_SOURCE_INVALID"));
}

TEST_F(ReportPreflightRepositoryTest, AComparisonThatBecameInvalidIsBlocked) {
    const auto previous = id(
        "insert into inspection_years(bridge_id, inspection_year, status, is_current) "
        "values ($1::uuid, 2025, '已确认', true) returning id", bridge_id_);
    client_->execSqlSync(
        "update inspection_years set report_comparison_inspection_id=$2::uuid where id=$1::uuid",
        year_id_, previous);
    client_->execSqlSync(
        "update inspection_years set is_current=false, status='已被修订' where id=$1::uuid",
        previous);

    EXPECT_TRUE(has(evaluate(), "REPORT_COMPARISON_SOURCE_INVALID"));
}

TEST_F(ReportPreflightRepositoryTest, PhotoWithoutAnArchivedFileIsBlocked) {
    const auto defect = add_defect(component_id_, "上部结构");
    client_->execSqlSync(
        "insert into defect_photos(defect_observation_id, photo_number) values ($1::uuid, '2.1-1')",
        defect);

    const auto result = evaluate();
    EXPECT_TRUE(has(result, "REPORT_PHOTO_UNREADABLE"));
    EXPECT_EQ(result.photo_count, 1);
}

TEST_F(ReportPreflightRepositoryTest, CannotGenerateWhileAnyBlockingFindingStands) {
    // 基线就带着 REPORT_ASSESSMENT_REQUIRED，这里顺带守住 can_generate 的语义。
    const auto result = evaluate();
    EXPECT_FALSE(result.can_generate());
    EXPECT_FALSE(result.to_json()["can_generate"].asBool());
}

}  // namespace
