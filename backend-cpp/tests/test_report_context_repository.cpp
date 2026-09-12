#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/ReportContextRepository.hpp"
#include "bridge_report/standards/StandardPackageLoader.hpp"
#include "bridge_report/standards/StandardRegistry.hpp"

namespace {

// 规范包里的真实部件类别编号。权重表（表4.1-1）要拿评定结果的类别去对规范清单，
// 编号对不上就会把每一行都判成"无此构件"，所以夹具必须用真编号。
constexpr const char* kUpperBearing = "h21.component.beam.upper_bearing";
constexpr const char* kPier = "h21.component.lower.pier";
constexpr const char* kPavement = "h21.component.deck.pavement";
constexpr const char* kBeamBridge = "h21.bridge_type.beam";

drogon::orm::DbClientPtr shared_test_client() {
    static drogon::orm::DbClientPtr client = [] {
        const bridge_report::config::PostgresConfig config{};
        return bridge_report::db::create_db_client(config, 1);
    }();
    return client;
}

// ReportContext 组装的集成夹具（设计 §9.2、§10.2、§11）。
//
// 这里守两件事：三级排序，以及报告图号的现编与一致性——病害表的「照片编号」列
// 和图题必须是同一批号码，两处对不上读者就按号找不到图。
class ReportContextRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL 未设置，跳过需要真实数据库的集成测试";
        }
        client_ = shared_test_client();
        tag_ = ::testing::UnitTest::GetInstance()->current_test_info()->name();

        user_id_ = id("insert into users(username, display_name, password_hash, role) "
                      "values ($1,'上下文测试员','x','admin') returning id", "ctx_" + tag_);
        bridge_id_ = id("insert into bridges(bridge_name, route_number, route_name, "
                        " administrative_region) "
                        "values ('上下文测试桥','S320','大养线','太和区') returning id");
        revision_id_ = id("insert into bridge_component_inventory_revisions"
                          "(bridge_id, revision_number, status, created_by_user_id) "
                          "values ($1::uuid,1,'草稿',$2::uuid) returning id", bridge_id_, user_id_);

        create_standards();

        // 台账顺序故意与创建顺序相反：排序必须跟 sort_order 走，而不是插入次序。
        // 规范映射必须在确认修订版之前挂上：已确认的修订版彻底不可变，事后补不进去。
        beam_b_ = component("上部结构", "1-2#板");
        mapping(entry(beam_b_, "1-2#板", 20), kUpperBearing, "superstructure");
        beam_a_ = component("上部结构", "1-1#板");
        mapping(entry(beam_a_, "1-1#板", 10), kUpperBearing, "superstructure");
        deck_ = component("桥面系", "1#跨桥面铺装");
        mapping(entry(deck_, "1#跨桥面铺装", 30), kPavement, "deck_system");
        pier_ = component("下部结构", "2#墩");
        mapping(entry(pier_, "2#墩", 40), kPier, "substructure");
        client_->execSqlSync("update bridge_component_inventory_revisions set status='已确认', "
                             " confirmed_by_user_id=$2::uuid, confirmed_at=now() where id=$1::uuid",
                             revision_id_, user_id_);

        year_id_ = id("insert into inspection_years(bridge_id, inspection_year, status, is_current, "
                      " component_inventory_revision_id, standard_profile_id, report_number, "
                      " project_name, inspection_org) "
                      "values ($1::uuid, 2026, '已确认', true, $2::uuid, $3::uuid, 'Q2026-1', "
                      " '定检A4标段', '某某院') returning id",
                      bridge_id_, revision_id_, profile_id_);

        file_id_ = id("insert into archived_files(bridge_id, original_file_name, current_file_name, "
                      " storage_relative_path, file_type, file_purpose) "
                      "values ($1::uuid,'t.docx','t.docx',$2,'模板','报告模板') returning id",
                      bridge_id_, "templates/ctx-" + tag_ + "/t.docx");
        template_id_ = id(
            "insert into report_templates(template_code, template_name, file_id, file_checksum, "
            " contract_config_json, validation_status, is_enabled, updated_by_user_id) "
            "values ($1,'上下文模板',$2::uuid,$3, $4::jsonb, 'valid', true, $5::uuid) returning id",
            "CTX_" + tag_, file_id_, "sha256:" + std::string(64, 'a'),
            R"({"table_number_formats":{
                  "DEFECT_PHOTOS:SUPERSTRUCTURE":"照片2.1-{n}",
                  "DEFECT_PHOTOS:SUBSTRUCTURE":"照片2.2-{n}",
                  "DEFECT_PHOTOS:DECK":"照片2.3-{n}"}})",
            user_id_);
        client_->execSqlSync(
            "insert into inspection_report_settings(inspection_year_id, template_id) "
            "values ($1::uuid,$2::uuid)", year_id_, template_id_);
    }

    void TearDown() override {
        if (client_ == nullptr) return;
        client_->execSqlSync("update inspection_years set report_comparison_inspection_id=null "
                             "where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_report_settings where inspection_year_id in "
                             "(select id from inspection_years where bridge_id=$1::uuid)", bridge_id_);
        client_->execSqlSync("delete from defect_photos where defect_observation_id in "
                             "(select id from defect_observations where bridge_id=$1::uuid)", bridge_id_);
        client_->execSqlSync("delete from defect_observations where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from report_templates where id=$1::uuid", template_id_);
        // 建过正式评定的年度不再删：完成的正式评定不可删改（迁移 013 的保护触发器），
        // 年度被它 restrict 住。隔离 schema 跑完会整个 drop，留着无妨。
        if (run_id_.empty()) {
            client_->execSqlSync("delete from inspection_years where bridge_id=$1::uuid",
                                 bridge_id_);
        }
        if (run_id_.empty()) {
            client_->execSqlSync("delete from archived_files where id=$1::uuid", file_id_);
        }
        // 台账修订版、条目、构件、桥梁、用户不清理：已确认的修订版彻底不可变，
        // 隔离 schema 跑完会整个 drop（同 ReportPreflightRepositoryTest 的说明）。
    }

    template <typename... Args>
    std::string id(const std::string& sql, Args&&... args) {
        return client_->execSqlSync(sql, std::forward<Args>(args)...)[0]["id"]
            .template as<std::string>();
    }

    std::string component(const std::string& part, const std::string& code) {
        return id("insert into bridge_components(bridge_id, structure_part, component_type, "
                  " business_component_code, normalized_component_key, creation_source) "
                  "values($1::uuid,$2,'构件',$3,$4,'人工录入') returning id",
                  bridge_id_, part, code, "ctx-" + tag_ + "-" + code);
    }

    std::string entry(const std::string& component_id, const std::string& number,
                      int sort_order) {
        return id(
            "insert into bridge_component_inventory_entries(inventory_revision_id, "
            " bridge_component_id, component_number, site_name, site_component_type, sort_order) "
            "values ($1::uuid,$2::uuid,$3,'测点','构件',$4) returning id",
            revision_id_, component_id, number, sort_order);
    }

    /// 台账条目到规范部件类别的映射。评定结果必须与它一致，否则迁移 013 的
    /// validate_assessment_component_result_context 会拒绝写入。
    void mapping(const std::string& entry_id, const std::string& category,
                 const std::string& structure_part) {
        client_->execSqlSync(
            "insert into bridge_component_standard_mappings(inventory_entry_id, "
            " standard_package_id, standard_bridge_type_id, standard_component_category_id, "
            " structure_part, mapping_source, confirmation_status, confirmed_by_user_id, "
            " confirmed_at) "
            "values ($1::uuid,$2::uuid,'beam_bridge',$3,$4,'上下文测试','已确认',$5::uuid,now())",
            entry_id, tech_package_id_, category, structure_part, user_id_);
    }

    /// 规范包、已发布评定树和项目规范配置。台账映射与正式评定都挂在它们上面，
    /// 所以必须在确认修订版之前建好。
    void create_standards() {
        tech_package_id_ = id(
            "insert into standard_packages(standard_family, standard_id, standard_code, "
            " standard_name, official_edition, package_version, contract_version, algorithm_id, "
            " effective_date, content_checksum) "
            "values ('technical_condition',$1,'TEST 21','上下文测试技术规范','2026','1.0.0',1,"
            " 'test-tech','2026-01-01',$2) returning id",
            "CTX-TECH-" + tag_, "sha256:" + std::string(64, 'a'));
        const auto maintenance = id(
            "insert into standard_packages(standard_family, standard_id, standard_code, "
            " standard_name, official_edition, package_version, contract_version, algorithm_id, "
            " effective_date, content_checksum) "
            "values ('maintenance',$1,'TEST 5120','上下文测试养护规范','2026','1.0.0',1,"
            " 'test-maint','2026-01-01',$2) returning id",
            "CTX-MAINT-" + tag_, "sha256:" + std::string(64, 'b'));
        const auto tree = id(
            "insert into rating_tree_versions("
            "tree_code,tree_name,package_version,contract_version,"
            "technical_condition_package_id,technical_condition_standard_id,"
            "technical_condition_package_version,technical_condition_content_checksum,"
            "maintenance_package_id,maintenance_standard_id,"
            "maintenance_package_version,maintenance_content_checksum,"
            "organization_tree_code,organization_package_version,"
            "organization_content_checksum,tree_content_checksum,status"
            ") select $1,'上下文测试评定树','1.0.0',1,$2::uuid,t.standard_id,t.package_version,"
            "       t.content_checksum,$3::uuid,m.standard_id,m.package_version,m.content_checksum,"
            "       $1,'1.0.0',"
            "       'sha256:'||md5(gen_random_uuid()::text)||md5(gen_random_uuid()::text),"
            "       'sha256:'||md5(gen_random_uuid()::text)||md5(gen_random_uuid()::text),'draft' "
            "from standard_packages t, standard_packages m "
            "where t.id=$2::uuid and m.id=$3::uuid returning id",
            "CTX-TREE-" + tag_, tech_package_id_, maintenance);
        client_->execSqlSync(
            "insert into rating_tree_nodes(rating_tree_version_id,node_key,display_name,"
            " node_type,scoring_mode,is_selectable) "
            "values ($1::uuid,'root','桥梁评定','root','non_scoring',false)", tree);
        // published 要求 published_at 非空（018 的 publish_state_check），故分两步。
        client_->execSqlSync(
            "update rating_tree_versions set status='published', published_at=now() "
            "where id=$1::uuid", tree);
        profile_id_ = id(
            "insert into project_standard_profiles(technical_condition_package_id, "
            " maintenance_package_id, rating_tree_version_id, created_by_user_id, change_reason) "
            "values ($1::uuid,$2::uuid,$3::uuid,$4::uuid,'上下文测试') returning id",
            tech_package_id_, maintenance, tree, user_id_);
    }

    std::string defect(const std::string& component_id, const std::string& part,
                       const std::string& type = "裂缝", const std::string& origin = "",
                       const std::string& indicator = "") {
        return id("insert into defect_observations(inspection_year_id, bridge_id, "
                  " bridge_component_id, structure_part, defect_type, defect_description_raw, "
                  " source_raw_cells_json, standard_defect_indicator_id) "
                  "values ($1::uuid,$2::uuid,$3::uuid,$4,$5,'测试病害', "
                  "        coalesce(nullif($6::text,'')::jsonb,'{}'::jsonb), "
                  "        nullif($7::text,'')) returning id",
                  year_id_, bridge_id_, component_id, part, type, origin, indicator);
    }

    void photo(const std::string& defect_id, const std::string& source_number,
               const std::string& title) {
        client_->execSqlSync(
            "insert into defect_photos(defect_observation_id, archived_file_id, photo_number, "
            " photo_title) values ($1::uuid,$2::uuid,nullif($3::text,''),nullif($4::text,''))",
            defect_id, file_id_, source_number, title);
    }

    /// 装着真实 H21 规范包的注册表，但登记在本次测试那份规范包行的身份下。
    ///
    /// 权重表是按评定运行锁定的 (standard_id, package_version) 去注册表里找包的；
    /// 夹具每个用例建一份自己的规范包行（identity 唯一约束不允许重名），所以这里
    /// 把真包的清单挂到那个身份上，走的仍是生产同一条查找路径。
    std::shared_ptr<const bridge_report::standards::StandardRegistry> registry_with_h21() {
        const auto identity = client_->execSqlSync(
            "select standard_id, package_version from standard_packages where id=$1::uuid",
            tech_package_id_)[0];

        bridge_report::standards::StandardPackageLoader loader;
        auto loaded = loader.load(std::filesystem::path(BRIDGE_REPORT_REPOSITORY_ROOT) /
                                  "standards/technical-condition/jtg-t-h21-2011/1.0.4");
        if (!loaded.ok()) return nullptr;
        loaded.package->manifest.standard_id = identity["standard_id"].as<std::string>();
        loaded.package->manifest.package_version =
            identity["package_version"].as<std::string>();

        auto registry = std::make_shared<bridge_report::standards::StandardRegistry>();
        registry->register_package(std::move(*loaded.package));
        return registry;
    }

    bridge_report::report::ReportContext build(
        std::shared_ptr<const bridge_report::standards::StandardRegistry> registry = nullptr) {
        const auto context =
            bridge_report::db::ReportContextRepository(client_, std::move(registry))
                .build(year_id_);
        if (!context.has_value()) {
            ADD_FAILURE() << "上下文组装失败";
            return {};
        }
        return *context;
    }

    static const bridge_report::report::ReportStructurePart* part_of(
        const bridge_report::report::ReportContext& context, const std::string& code) {
        for (const auto& part : context.parts) {
            if (part.part_code == code) return &part;
        }
        return nullptr;
    }

    /// 造一次当前正式评定（设计 §13）。只有需要评定结果的用例才调用。
    void enable_assessment() {
        run_id_ = id(
            "insert into assessment_runs(inspection_year_id, run_kind, result_status, "
            " input_summary_json, input_checksum, rule_package_summary_json, "
            " rule_package_checksum, created_by_user_id, formal_revision_number, "
            " technical_condition_package_id, standard_profile_id, "
            " component_inventory_revision_id, rating_tree_version_id, "
            " rating_tree_content_checksum) "
            "values ($1::uuid,'正式','运行中','{\"source\":\"ctx-test\"}'::jsonb,$2,"
            " '{\"package\":\"ctx-test\"}'::jsonb,$3,$4::uuid,1,$5::uuid,$6::uuid,$7::uuid,"
            " (select rating_tree_version_id from project_standard_profiles where id=$6::uuid),"
            " (select v.tree_content_checksum from rating_tree_versions v "
            "  join project_standard_profiles p on p.rating_tree_version_id=v.id "
            "  where p.id=$6::uuid)) returning id",
            // 迁移 013 的触发器要求 rule_package_checksum 等于所锁技术规范包的
            // content_checksum，两者必须是同一个值。
            year_id_, "sha256:" + std::string(64, 'c'), "sha256:" + std::string(64, 'a'),
            user_id_, tech_package_id_, profile_id_, revision_id_);
    }

    /// 一条构件评定结果；defects 里的每个 (指标, 扣分) 就是评定引擎实际算出来的那一组。
    /// 类别与部位必须与台账映射一致，否则迁移 013 的上下文触发器会拒绝写入。
    void component_result(const std::string& component_id, double score,
                          const std::string& defects_json,
                          const std::string& category = kUpperBearing,
                          const std::string& structure_part = "superstructure") {
        client_->execSqlSync(
            "insert into assessment_component_results(assessment_run_id, bridge_component_id, "
            " standard_component_category_id, structure_part, score, result_json) "
            "values ($1::uuid,$2::uuid,$5,$6,$3,$4::jsonb)",
            run_id_, component_id, score, "{\"defects\":" + defects_json + "}",
            category, structure_part);
    }

    void part_result(const std::string& level, const std::string& key,
                     const std::string& structure_part, double score,
                     const std::string& grade, const std::string& detail = "{}") {
        client_->execSqlSync(
            "insert into assessment_part_results(assessment_run_id, result_level, result_key, "
            " structure_part, score, grade, weight, result_json) "
            "values ($1::uuid,$2,$3,$4,$5,$6,0.4,$7::jsonb)",
            run_id_, level, key, structure_part, score, grade, detail);
    }

    /// 桥型记在 result_summary_json 里，权重表按它去规范包取完整部件清单。
    void finish_assessment(const std::string& bridge_type_id = kBeamBridge) {
        client_->execSqlSync(
            "update assessment_runs set result_status='成功', is_current=true, "
            " confirmed_by_user_id=$2::uuid, confirmed_at=now(), "
            " result_summary_json=jsonb_build_object('result', "
            "   jsonb_build_object('bridge_type_id', $3::text)) where id=$1::uuid",
            run_id_, user_id_, bridge_type_id);
    }

    drogon::orm::DbClientPtr client_;
    std::string tag_, user_id_, bridge_id_, revision_id_, year_id_, file_id_, template_id_;
    std::string beam_a_, beam_b_, deck_, pier_, run_id_, tech_package_id_, profile_id_;
};

TEST_F(ReportContextRepositoryTest, ReturnsNothingWhenNoTemplateIsConfigured) {
    client_->execSqlSync("update inspection_report_settings set template_id=null "
                         "where inspection_year_id=$1::uuid", year_id_);

    EXPECT_FALSE(bridge_report::db::ReportContextRepository(client_).build(year_id_).has_value());
}

TEST_F(ReportContextRepositoryTest, CarriesTheScalarPlaceholderValues) {
    const auto context = build();

    EXPECT_EQ(context.report_no, "Q2026-1");
    EXPECT_EQ(context.bridge_name, "上下文测试桥");
    ASSERT_TRUE(context.route_code.has_value());
    EXPECT_EQ(*context.route_code, "S320");
    ASSERT_TRUE(context.administrative_region.has_value());
    EXPECT_EQ(*context.administrative_region, "太和区");
    EXPECT_EQ(context.inspection_year, 2026);
    // 数据库里没有报告日期，构造时写定一次。
    EXPECT_FALSE(context.report_date.empty());
    EXPECT_EQ(context.to_json()["scalars"]["report_date"].asString(), context.report_date);
}

TEST_F(ReportContextRepositoryTest, PartsFollowTheDesignedOrder) {
    defect(deck_, "桥面系");
    defect(pier_, "下部结构");
    defect(beam_a_, "上部结构");

    const auto context = build();

    std::vector<std::string> codes;
    for (const auto& part : context.parts) codes.push_back(part.part_code);
    // 上部、下部、桥面系——与插入顺序无关（设计 §10.2）。
    EXPECT_EQ(codes, (std::vector<std::string>{"SUPERSTRUCTURE", "SUBSTRUCTURE", "DECK"}));
}

TEST_F(ReportContextRepositoryTest, DefectRowsFollowTheInventorySortOrder) {
    // 先插 1-2#板（sort_order 20），后插 1-1#板（sort_order 10）。
    defect(beam_b_, "上部结构");
    defect(beam_a_, "上部结构");

    const auto context = build();
    const auto* part = part_of(context, "SUPERSTRUCTURE");
    ASSERT_NE(part, nullptr);
    ASSERT_EQ(part->defect_rows.size(), 2u);
    // 排序跟台账自然编号走，不跟插入次序。
    EXPECT_EQ(*part->defect_rows[0].component_number, "1-1#板");
    EXPECT_EQ(*part->defect_rows[1].component_number, "1-2#板");
    EXPECT_EQ(part->defect_rows[0].row_number, 1);
    EXPECT_EQ(part->defect_rows[1].row_number, 2);
}

TEST_F(ReportContextRepositoryTest, PhotoNumbersRestartPerStructurePart) {
    const auto beam = defect(beam_a_, "上部结构");
    photo(beam, "9.9-7", "梁底裂缝");
    const auto slab = defect(deck_, "桥面系");
    photo(slab, "9.9-8", "铺装坑槽");

    const auto context = build();
    const auto* super = part_of(context, "SUPERSTRUCTURE");
    const auto* deck = part_of(context, "DECK");
    ASSERT_NE(super, nullptr);
    ASSERT_NE(deck, nullptr);

    // 每个部位各自从 1 起排，套模板配置的前缀（设计 §7.5、§11.2）。
    EXPECT_EQ(super->photos[0].report_number, "照片2.1-1");
    EXPECT_EQ(deck->photos[0].report_number, "照片2.3-1");
}

TEST_F(ReportContextRepositoryTest, ReportNumbersIgnoreTheStoredSourceNumber) {
    const auto beam = defect(beam_a_, "上部结构");
    // 入库编号是乱的（导入器按 UUID 顺序发的），报告不能用它。
    photo(beam, "2.1-47", "第一张");
    photo(beam, "2.1-5", "第二张");

    const auto context = build();
    const auto* part = part_of(context, "SUPERSTRUCTURE");
    ASSERT_NE(part, nullptr);
    ASSERT_EQ(part->photos.size(), 2u);
    EXPECT_EQ(part->photos[0].report_number, "照片2.1-1");
    EXPECT_EQ(part->photos[1].report_number, "照片2.1-2");
    // 来源编号仍带在上下文里，仅作排障用的来源证据。
    EXPECT_EQ(*part->photos[0].source_photo_number, "2.1-47");
}

TEST_F(ReportContextRepositoryTest, DefectTablePhotoColumnMatchesTheCaptions) {
    // 本方案唯一的致命失败模式：两处号码对不上，读者按表里的号就找不到图（§11.8）。
    const auto first = defect(beam_a_, "上部结构");
    photo(first, "", "甲一");
    photo(first, "", "甲二");
    const auto second = defect(beam_b_, "上部结构");
    photo(second, "", "乙一");

    const auto context = build();
    const auto* part = part_of(context, "SUPERSTRUCTURE");
    ASSERT_NE(part, nullptr);

    std::vector<std::string> from_table;
    for (const auto& row : part->defect_rows) {
        for (const auto& number : row.photo_numbers) from_table.push_back(number);
    }
    std::vector<std::string> from_photos;
    for (const auto& photo : part->photos) from_photos.push_back(photo.report_number);

    EXPECT_EQ(from_table, from_photos);
    EXPECT_EQ(from_table, (std::vector<std::string>{"照片2.1-1", "照片2.1-2", "照片2.1-3"}));
}

TEST_F(ReportContextRepositoryTest, CaptionUsesTwoSpacesAndFallsBackWhenTitleIsEmpty) {
    const auto beam = defect(beam_a_, "上部结构");
    photo(beam, "", "1-1#板横向裂缝");
    photo(beam, "", "");

    const auto context = build();
    const auto* part = part_of(context, "SUPERSTRUCTURE");
    ASSERT_NE(part, nullptr);
    ASSERT_EQ(part->photos.size(), 2u);
    // 与两份正式报告一致：「照片2.1-1␠␠1-1#板横向裂缝」。
    EXPECT_EQ(part->photos[0].caption(), "照片2.1-1  1-1#板横向裂缝");
    // 标题为空时只输出图号，不得用病害描述臆造标题（设计 §11.7）。
    EXPECT_EQ(part->photos[1].caption(), "照片2.1-2");
}

TEST_F(ReportContextRepositoryTest, ComparisonCountsDeduplicateRangeSplitSiblings) {
    const std::string origin = R"({"range_split_origin":{"source_candidate_id":"defect_0001"}})";
    defect(beam_a_, "上部结构", "裂缝", origin);
    defect(beam_b_, "上部结构", "裂缝", origin);
    defect(beam_a_, "上部结构");

    const auto context = build();
    const auto* part = part_of(context, "SUPERSTRUCTURE");
    ASSERT_NE(part, nullptr);
    EXPECT_EQ(part->defect_rows.size(), 3u);
    // 三条观测，但只来自两条来源病害（设计 §12.2）。
    EXPECT_EQ(part->comparison.current_source_defect_count, 2);
    EXPECT_EQ(context.overall_comparison.current_source_defect_count, 2);
}

TEST_F(ReportContextRepositoryTest, ComparisonDeltaIsZeroWithoutAHistorySelection) {
    defect(beam_a_, "上部结构");

    const auto context = build();
    EXPECT_FALSE(context.overall_comparison.has_previous);
    EXPECT_EQ(context.overall_comparison.previous_source_defect_count, 0);
}

TEST_F(ReportContextRepositoryTest, ComparisonAgainstTheSelectedHistoryYear) {
    const auto previous = id(
        "insert into inspection_years(bridge_id, inspection_year, status, is_current, "
        " component_inventory_revision_id) values ($1::uuid,2025,'已确认',true,$2::uuid) returning id",
        bridge_id_, revision_id_);
    client_->execSqlSync(
        "insert into defect_observations(inspection_year_id, bridge_id, bridge_component_id, "
        " structure_part, defect_type, defect_description_raw) "
        "values ($1::uuid,$2::uuid,$3::uuid,'上部结构','裂缝','旧病害')",
        previous, bridge_id_, beam_a_);
    client_->execSqlSync(
        "update inspection_years set report_comparison_inspection_id=$2::uuid where id=$1::uuid",
        year_id_, previous);

    defect(beam_a_, "上部结构");
    defect(beam_b_, "上部结构");

    const auto context = build();
    const auto* part = part_of(context, "SUPERSTRUCTURE");
    ASSERT_NE(part, nullptr);
    EXPECT_TRUE(part->comparison.has_previous);
    EXPECT_EQ(part->comparison.current_source_defect_count, 2);
    EXPECT_EQ(part->comparison.previous_source_defect_count, 1);
    EXPECT_EQ(part->comparison.delta, 1);
    ASSERT_TRUE(context.comparison_year.has_value());
    EXPECT_EQ(*context.comparison_year, 2025);
}

TEST_F(ReportContextRepositoryTest, BuildingTwiceProducesIdenticalOutput) {
    const auto beam = defect(beam_a_, "上部结构");
    photo(beam, "2.1-9", "甲");
    photo(beam, "2.1-3", "乙");
    defect(beam_b_, "上部结构");
    defect(deck_, "桥面系");

    // 同样的输入必须得到完全相同的顺序、编号和文字（设计 §5.5）。
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    const auto first = Json::writeString(writer, build().to_json());
    const auto second = Json::writeString(writer, build().to_json());

    EXPECT_EQ(first, second);
}

TEST_F(ReportContextRepositoryTest, CarriesPersonnelAndEquipment) {
    const auto person = id("insert into report_personnel(full_name, professional_title) "
                           "values ('张三','高级工程师') returning id");
    const auto gear = id("insert into report_equipment(equipment_name, model_spec) "
                         "values ('裂缝观测仪','ZBL-F103') returning id");
    client_->execSqlSync("insert into inspection_report_personnel(inspection_year_id, personnel_id, "
                         " role_code) values ($1::uuid,$2::uuid,'approver')", year_id_, person);
    client_->execSqlSync("insert into inspection_report_equipment(inspection_year_id, equipment_id, "
                         " purpose) values ($1::uuid,$2::uuid,'裂缝宽度测量')", year_id_, gear);

    const auto context = build();
    ASSERT_EQ(context.personnel.size(), 1u);
    EXPECT_EQ(context.personnel[0].full_name, "张三");
    EXPECT_EQ(context.personnel[0].role_code, "approver");
    ASSERT_EQ(context.equipment.size(), 1u);
    EXPECT_EQ(context.equipment[0].equipment_name, "裂缝观测仪");
    ASSERT_TRUE(context.equipment[0].purpose.has_value());
    EXPECT_EQ(*context.equipment[0].purpose, "裂缝宽度测量");

    client_->execSqlSync("delete from inspection_report_personnel where inspection_year_id=$1::uuid",
                         year_id_);
    client_->execSqlSync("delete from inspection_report_equipment where inspection_year_id=$1::uuid",
                         year_id_);
    client_->execSqlSync("delete from report_personnel where id=$1::uuid", person);
    client_->execSqlSync("delete from report_equipment where id=$1::uuid", gear);
}

// ---- 当前正式评定（设计 §13）-------------------------------------------------

TEST_F(ReportContextRepositoryTest, ReportsNoAssessmentWhenThereIsNoFormalRun) {
    defect(beam_a_, "上部结构");

    const auto context = build();

    EXPECT_FALSE(context.assessment.has_formal_run);
    EXPECT_FALSE(context.assessment.overall_score.has_value());
    EXPECT_TRUE(context.assessment.parts.empty());
    // 没有评定时扣分与构件评分都留空——"没有依据"，不是"扣了 0 分"。
    EXPECT_FALSE(context.parts[0].defect_rows[0].deduction.has_value());
    EXPECT_FALSE(context.parts[0].defect_rows[0].component_score.has_value());
}

TEST_F(ReportContextRepositoryTest, CarriesTheFormalAssessmentResults) {
    defect(beam_a_, "上部结构");
    enable_assessment();
    component_result(beam_a_, 85.5, "[]");
    part_result("部件", kUpperBearing, "superstructure", 85.5, "2类",
                R"({"component_type_name":"上部承重构件"})");
    part_result("结构", "superstructure", "superstructure", 85.5, "2类");
    part_result("全桥", "overall", "overall", 86.4, "2类");
    finish_assessment();

    const auto context = build();

    ASSERT_TRUE(context.assessment.has_formal_run);
    ASSERT_TRUE(context.assessment.overall_score.has_value());
    EXPECT_DOUBLE_EQ(*context.assessment.overall_score, 86.4);
    ASSERT_EQ(context.assessment.parts.size(), 1u);
    EXPECT_EQ(context.assessment.parts[0].part_code, "SUPERSTRUCTURE");
    EXPECT_EQ(context.assessment.parts[0].part_label, "上部结构");
    ASSERT_EQ(context.assessment.categories.size(), 1u);
    ASSERT_TRUE(context.assessment.categories[0].category_name.has_value());
    EXPECT_EQ(*context.assessment.categories[0].category_name, "上部承重构件");
    // 构件数量来自评定结果，不是数台账。
    EXPECT_EQ(context.assessment.categories[0].component_count, 1);
    // 构件评分取自评定，不取病害行上那两个已被迁移 014 删掉的导入遗留列。
    ASSERT_TRUE(context.parts[0].defect_rows[0].component_score.has_value());
    EXPECT_DOUBLE_EQ(*context.parts[0].defect_rows[0].component_score, 85.5);
}

TEST_F(ReportContextRepositoryTest, DeductionIsCountedOncePerIndicatorGroup) {
    // 同一构件、同一指标的两条病害：H21 只按最重标度扣一次。
    defect(beam_a_, "上部结构", "裂缝", "", "H21-CRACK");
    defect(beam_a_, "上部结构", "裂缝", "", "H21-CRACK");
    defect(beam_a_, "上部结构", "剥落", "", "H21-SPALL");
    enable_assessment();
    component_result(
        beam_a_, 85.5,
        R"([{"defect_indicator_id":"H21-CRACK","deduction":5.0},
            {"defect_indicator_id":"H21-SPALL","deduction":3.0}])");
    finish_assessment();

    const auto context = build();
    const auto* part = part_of(context, "SUPERSTRUCTURE");
    ASSERT_NE(part, nullptr);
    ASSERT_EQ(part->defect_rows.size(), 3u);

    // 组内第一条带扣分，第二条是 0——0 表示"按规范没有额外扣分"，与"没有依据"
    // 的空值不是一回事，读者把这一列加起来才对得上。
    ASSERT_TRUE(part->defect_rows[0].deduction.has_value());
    EXPECT_DOUBLE_EQ(*part->defect_rows[0].deduction, 5.0);
    ASSERT_TRUE(part->defect_rows[1].deduction.has_value());
    EXPECT_DOUBLE_EQ(*part->defect_rows[1].deduction, 0.0);
    ASSERT_TRUE(part->defect_rows[2].deduction.has_value());
    EXPECT_DOUBLE_EQ(*part->defect_rows[2].deduction, 3.0);
}

TEST_F(ReportContextRepositoryTest, TopDeductionsComeFromTheAssembledDefectRows) {
    defect(beam_a_, "上部结构", "裂缝", "", "H21-CRACK");
    defect(beam_b_, "上部结构", "露筋", "", "H21-REBAR");
    enable_assessment();
    component_result(beam_a_, 85.5, R"([{"defect_indicator_id":"H21-CRACK","deduction":5.0}])");
    component_result(beam_b_, 70.0, R"([{"defect_indicator_id":"H21-REBAR","deduction":12.0}])");
    finish_assessment();

    const auto context = build();

    // 扣分从大到小；点名的病害必须就是病害表里的那几行，两处不能各查各的。
    ASSERT_EQ(context.assessment.top_deductions.size(), 2u);
    EXPECT_EQ(context.assessment.top_deductions[0].defect_type, "露筋");
    EXPECT_DOUBLE_EQ(context.assessment.top_deductions[0].deduction, 12.0);
    EXPECT_EQ(context.assessment.top_deductions[1].defect_type, "裂缝");
    EXPECT_EQ(*context.assessment.top_deductions[0].component_number, "1-2#板");
}

TEST_F(ReportContextRepositoryTest, GroupsComponentScoresIntoBands) {
    // 表4.1-2 在每个评价部件下按构件评分分档：同分的构件合成一行。
    defect(beam_a_, "上部结构");
    enable_assessment();
    component_result(beam_a_, 75.0, "[]");
    component_result(beam_b_, 100.0, "[]");
    part_result("部件", kUpperBearing, "superstructure", 83.3, "2类",
                R"({"component_type_name":"上部承重构件"})");
    finish_assessment();

    const auto context = build();

    ASSERT_EQ(context.assessment.categories.size(), 1u);
    const auto& bands = context.assessment.categories[0].score_bands;
    ASSERT_EQ(bands.size(), 2u);
    // 分数由低到高，正式报告先列最差的那一档。
    EXPECT_DOUBLE_EQ(bands[0].score, 75.0);
    EXPECT_EQ(bands[0].component_count, 1);
    EXPECT_DOUBLE_EQ(bands[1].score, 100.0);
    EXPECT_EQ(bands[1].component_count, 1);
}

TEST_F(ReportContextRepositoryTest, ReportsNoTriggeredControlIndicatorByDefault) {
    defect(beam_a_, "上部结构");
    enable_assessment();
    component_result(beam_a_, 85.5, "[]");
    finish_assessment();

    const auto context = build();

    // 一条都没触发，就是"不符合 4.3.1 的所有规定"，不是数据缺失。
    EXPECT_TRUE(context.assessment.triggered_controls.empty());
}

TEST_F(ReportContextRepositoryTest, CarriesTriggeredControlIndicators) {
    defect(beam_a_, "上部结构");
    enable_assessment();
    component_result(beam_a_, 30.0, "[]");
    client_->execSqlSync(
        "insert into assessment_control_results(assessment_run_id, rule_id, control_level, "
        " target_key, triggered, grade_after, message) "
        "values ($1::uuid,'h21.control.main_component','全桥','overall',true,'5类',"
        " '上部主要构件评分低于 40 分。')",
        run_id_);
    // 未触发的不进报告：4.2 只讲符合了哪几条。
    client_->execSqlSync(
        "insert into assessment_control_results(assessment_run_id, rule_id, control_level, "
        " target_key, triggered, message) "
        "values ($1::uuid,'h21.control.bearing','全桥','overall',false,'支座未达控制条件。')",
        run_id_);
    finish_assessment();

    const auto context = build();

    ASSERT_EQ(context.assessment.triggered_controls.size(), 1u);
    EXPECT_EQ(context.assessment.triggered_controls[0].rule_id, "h21.control.main_component");
    ASSERT_TRUE(context.assessment.triggered_controls[0].grade_after.has_value());
    EXPECT_EQ(*context.assessment.triggered_controls[0].grade_after, "5类");
}

TEST_F(ReportContextRepositoryTest, WeightTableListsEveryStandardComponentIncludingAbsentOnes) {
    // 表4.1-1 的全部意义在于把"本桥没有的部件"也列出来：它们的权重正是被摊给同
    // 部位其余部件的那部分。评定结果里只有实际存在的部件，清单只能取自规范包。
    defect(beam_a_, "上部结构");
    enable_assessment();
    component_result(beam_a_, 75.0, "[]");
    component_result(beam_b_, 100.0, "[]");
    component_result(pier_, 90.0, "[]", kPier, "substructure");
    component_result(deck_, 80.0, "[]", kPavement, "deck_system");
    part_result("部件", kUpperBearing, "superstructure", 87.5, "1类");
    finish_assessment();

    const auto context = build(registry_with_h21());
    const auto& rows = context.assessment.component_weights;

    // 梁桥：上部三项、下部七项、桥面系六项，共 16 行，顺序即规范原表。
    ASSERT_EQ(rows.size(), 16u);
    EXPECT_EQ(rows[0].category_id, kUpperBearing);
    EXPECT_EQ(rows[0].order, 1);
    EXPECT_EQ(rows[9].category_id, "h21.component.lower.regulation_structure");
    EXPECT_EQ(rows[15].order, 16);

    // 有构件的行：构件数量来自评定结果里该类别的构件条数。
    EXPECT_TRUE(rows[0].present);
    ASSERT_TRUE(rows[0].category_name.has_value());
    // 权重表与病害表印同一个名字：两张表的部件名称对不上，读者就串不起来。
    EXPECT_EQ(*rows[0].category_name, "上部承重构件");
    ASSERT_TRUE(rows[0].component_count.has_value());
    EXPECT_EQ(*rows[0].component_count, 2);
    EXPECT_DOUBLE_EQ(rows[0].configured_weight, 0.70);

    // 本桥没有的部件：规范权重照印，重分配后权重与构件数量都空着。
    const auto& absent = rows[9];
    EXPECT_FALSE(absent.present);
    EXPECT_DOUBLE_EQ(absent.configured_weight, 0.02);
    EXPECT_FALSE(absent.effective_weight.has_value());
    EXPECT_FALSE(absent.component_count.has_value());
}

TEST_F(ReportContextRepositoryTest, PartNameComesFromTheStandardMappingNotTheImportedText) {
    // 病害表的「部件名称」是 H21 部件泛称，与「构件编号」不是一回事：盖梁归桥墩、
    // 铰缝归上部一般构件。实测三个年度 1197 行的 part_name 全都等于构件编号——
    // 来源软件那一列压根不是部件名称，直接印就成了两列一模一样。
    const auto beam = defect(beam_a_, "上部结构");
    client_->execSqlSync("update defect_observations set part_name='1-1#板' where id=$1::uuid",
                         beam);
    const auto pier_defect = defect(pier_, "下部结构");
    client_->execSqlSync("update defect_observations set part_name='2#墩' where id=$1::uuid",
                         pier_defect);
    enable_assessment();
    component_result(beam_a_, 85.0, "[]");
    component_result(pier_, 90.0, "[]", kPier, "substructure");
    finish_assessment();

    const auto context = build(registry_with_h21());

    const auto* super = part_of(context, "SUPERSTRUCTURE");
    ASSERT_NE(super, nullptr);
    ASSERT_TRUE(super->defect_rows[0].part_name.has_value());
    // 规范包写的是「上部承重构件（主梁、挂梁）」，括号里是举例说明；报告只印名称
    // 本身，否则病害表那一格 15 个字要折三四行（设计 §10.1）。
    EXPECT_EQ(*super->defect_rows[0].part_name, "上部承重构件");
    EXPECT_EQ(*super->defect_rows[0].component_number, "1-1#板");

    const auto* lower = part_of(context, "SUBSTRUCTURE");
    ASSERT_NE(lower, nullptr);
    ASSERT_TRUE(lower->defect_rows[0].part_name.has_value());
    EXPECT_EQ(*lower->defect_rows[0].part_name, "桥墩");
    EXPECT_EQ(*lower->defect_rows[0].component_number, "2#墩");
}

TEST_F(ReportContextRepositoryTest, PartNameFallsBackToTheImportedTextWithoutAPackage) {
    // 没有规范包时退回入库文字，不留空——Word 导入路的那一列可能本来就是泛称。
    const auto beam = defect(beam_a_, "上部结构");
    client_->execSqlSync("update defect_observations set part_name='上部承重构件' "
                         "where id=$1::uuid", beam);

    const auto context = build();

    const auto* super = part_of(context, "SUPERSTRUCTURE");
    ASSERT_NE(super, nullptr);
    ASSERT_TRUE(super->defect_rows[0].part_name.has_value());
    EXPECT_EQ(*super->defect_rows[0].part_name, "上部承重构件");
}

TEST_F(ReportContextRepositoryTest, WeightTableIsEmptyWithoutAStandardRegistry) {
    // 没有规范包就拼不出"本桥没有的部件"那几行；宁可整表不出，也不出一份
    // 只列了现有部件、看不出权重怎么重分配的半截表。
    defect(beam_a_, "上部结构");
    enable_assessment();
    component_result(beam_a_, 85.5, "[]");
    finish_assessment();

    const auto context = build();

    EXPECT_TRUE(context.assessment.component_weights.empty());
    EXPECT_TRUE(context.assessment.has_formal_run);
}

TEST_F(ReportContextRepositoryTest, CarriesTheBridgeProfileFacts) {
    client_->execSqlSync(
        "update bridges set bridge_type='钢筋混凝土简支板桥', span_combination='5×13', "
        " bridge_length_m=68.5, built_year=1998 where id=$1::uuid", bridge_id_);

    const auto context = build();

    ASSERT_TRUE(context.bridge_profile.bridge_type.has_value());
    EXPECT_EQ(*context.bridge_profile.bridge_type, "钢筋混凝土简支板桥");
    ASSERT_TRUE(context.bridge_profile.built_year.has_value());
    EXPECT_EQ(*context.bridge_profile.built_year, 1998);
    // 档案里没有的字段就是没有，不编默认值。
    EXPECT_FALSE(context.bridge_profile.maintenance_org.has_value());
}

TEST_F(ReportContextRepositoryTest, KeepsNonNumericScaleAsText) {
    // 标度是文本列。当成数字解析，一条"中等"就能让整次组装失败。
    client_->execSqlSync("update defect_observations set scale='中等' where id=$1::uuid",
                         defect(beam_a_, "上部结构"));

    const auto context = build();
    const auto* part = part_of(context, "SUPERSTRUCTURE");
    ASSERT_NE(part, nullptr);
    ASSERT_TRUE(part->defect_rows[0].scale.has_value());
    EXPECT_EQ(*part->defect_rows[0].scale, "中等");
}

}  // namespace
