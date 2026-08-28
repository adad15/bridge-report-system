#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/db/WordImportRepository.hpp"

namespace {

class WordImportRepositoryTest : public testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL is not set";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{});
        const auto bridge = client_->execSqlSync(
            "insert into bridges (bridge_name) values ('Word导入事务测试桥') returning id");
        bridge_id_ = bridge[0]["id"].as<std::string>();
        const auto year = client_->execSqlSync(
            "insert into inspection_years (bridge_id, inspection_year, status, is_current) "
            "values ($1::uuid, 2026, '待校对', true) returning id", bridge_id_);
        year_id_ = year[0]["id"].as<std::string>();
        const auto record = client_->execSqlSync(
            "insert into import_records (bridge_id, inspection_year_id, import_name, source_type, import_status) "
            "values ($1::uuid, $2::uuid, 'Word导入事务测试', '软件导出Word', '解析中') returning id",
            bridge_id_, year_id_);
        import_id_ = record[0]["id"].as<std::string>();
        const auto source = client_->execSqlSync(
            "with source_id as(select gen_random_uuid() id) "
            "insert into import_source_files (id,import_record_id,original_file_name,storage_relative_path,"
            "file_extension,file_size_bytes,file_hash,status,parsing_started_at) "
            "select id,$1::uuid,'report.docx',id::text||'.docx','.docx',9,$2,'解析中',now() from source_id "
            "returning id,storage_relative_path", import_id_, std::string(64, 'a'));
        source_file_id_ = source[0]["id"].as<std::string>();
        source_relative_path_ = source[0]["storage_relative_path"].as<std::string>();
        archive_root_ = std::filesystem::temp_directory_path() / ("bridge-word-parse-" + import_id_);
        std::filesystem::create_directories(archive_root_);
        std::ofstream(archive_root_ / source_relative_path_, std::ios::binary) << "fake-docx";
    }

    void TearDown() override {
        if (!client_) return;
        client_->execSqlSync("delete from import_source_files where id=$1::uuid", source_file_id_);
        client_->execSqlSync("delete from import_records where id = $1::uuid", import_id_);
        client_->execSqlSync("delete from archived_files where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where id = $1::uuid", year_id_);
        client_->execSqlSync("delete from bridges where id = $1::uuid", bridge_id_);
        if (!package_id_.empty()) {
            client_->execSqlSync("delete from standard_packages where id=$1::uuid", package_id_);
        }
        client_->closeAll();
        std::filesystem::remove_all(archive_root_);
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string year_id_;
    std::string source_file_id_;
    std::string source_relative_path_;
    std::string import_id_;
    std::string package_id_;
    std::filesystem::path archive_root_;
};

TEST_F(WordImportRepositoryTest, PersistsPhotosAndJsonInOneTransaction) {
    bridge_report::archive::ArchivedPhotoBatch batch;
    batch.data["contract"]["parser_name"] = "liaoning-word-importer";
    batch.data["contract"]["parser_version"] = "1.1.0";
    batch.data["photos"] = Json::Value(Json::arrayValue);
    batch.files.push_back({
        "photo_0001", "tmp.jpg", "photo_0001_hash.jpg", "tests/photos/photo_0001_hash.jpg",
        ".jpg", 128, std::string(64, 'a'), false
    });

    bridge_report::db::WordImportRepository repository(client_);
    const auto outcome = repository.persist_parse_result(import_id_, batch);

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;
    const auto rows = client_->execSqlSync(
        "select ir.import_status, ir.importer_name, ir.importer_version, ir.parsed_result_json::text as parsed, "
        "count(irf.id) as attachment_count from import_records ir "
        "left join import_record_files irf on irf.import_record_id = ir.id and irf.file_role = '附件' "
        "where ir.id = $1::uuid group by ir.id", import_id_);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0]["import_status"].as<std::string>(), "待校对");
    EXPECT_EQ(rows[0]["importer_name"].as<std::string>(), "liaoning-word-importer");
    EXPECT_EQ(rows[0]["importer_version"].as<std::string>(), "1.1.0");
    EXPECT_EQ(rows[0]["attachment_count"].as<long long>(), 1);
    EXPECT_NE(rows[0]["parsed"].as<std::string>().find("photos"), std::string::npos);
    const auto source = client_->execSqlSync(
        "select status,cleanup_reason from import_source_files where import_record_id=$1::uuid", import_id_);
    ASSERT_EQ(source.size(), 1u);
    EXPECT_EQ(source[0]["status"].as<std::string>(), "待清理");
    EXPECT_EQ(source[0]["cleanup_reason"].as<std::string>(), "解析成功");
}

TEST_F(WordImportRepositoryTest, PersistsExactDefectMatchAgainstConfirmedInventory) {
    const auto user_id = client_->execSqlSync(
        "select id::text from users where username='admin'")[0]["id"].as<std::string>();
    package_id_ = client_->execSqlSync(
        "insert into standard_packages(standard_family,standard_id,standard_code,standard_name,"
        "official_edition,package_version,contract_version,algorithm_id,effective_date,content_checksum) "
        "values('technical_condition','WORD-MATCH-'||gen_random_uuid()::text,'WORD MATCH',"
        "'Word匹配测试规范','2026','1.0.0',1,'word-match','2026-01-01',"
        "'sha256:'||repeat('b',64)) returning id::text")[0]["id"].as<std::string>();
    const auto component_id = client_->execSqlSync(
        "insert into bridge_components(bridge_id,structure_part,component_type,business_component_code,"
        "normalized_component_key,current_status,creation_source) values($1::uuid,'上部结构','主梁',"
        "'1-1#','word-match-1','已确认','人工录入') returning id::text",
        bridge_id_)[0]["id"].as<std::string>();
    const auto revision_id = client_->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,created_by_user_id) "
        "values($1::uuid,1,$2::uuid) returning id::text",
        bridge_id_, user_id)[0]["id"].as<std::string>();
    const auto entry_id = client_->execSqlSync(
        "insert into bridge_component_inventory_entries(inventory_revision_id,bridge_component_id,"
        "component_number,site_name,site_component_type,sort_order) "
        "values($1::uuid,$2::uuid,'1-1#梁','空心板','空心板',1) returning id::text",
        revision_id, component_id)[0]["id"].as<std::string>();
    client_->execSqlSync(
        "insert into bridge_component_standard_mappings(inventory_entry_id,standard_package_id,"
        "standard_bridge_type_id,standard_component_category_id,structure_part,mapping_source,"
        "confirmation_status,confirmed_by_user_id,confirmed_at) "
        "values($1::uuid,$2::uuid,'h21.bridge_type.beam','h21.component.beam.upper_bearing',"
        "'superstructure','规范模板','已确认',$3::uuid,now())",
        entry_id, package_id_, user_id);
    client_->execSqlSync(
        "update bridge_component_inventory_revisions set status='已确认',"
        "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
        revision_id, user_id);

    bridge_report::archive::ArchivedPhotoBatch batch;
    batch.data["contract"]["parser_name"] = "liaoning-word-importer";
    batch.data["contract"]["parser_version"] = "2.0.0";
    batch.data["photos"] = Json::Value(Json::arrayValue);
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "defect_0001";
    defect["component_name"] = "上部承重构件";  // 报告部件名称（规范固定用词）→ 类别
    defect["component_number"] = "1-1#梁";        // 含类型词，需原文保真
    defect["warnings"] = Json::Value(Json::arrayValue);
    batch.data["defects"].append(defect);

    bridge_report::db::WordImportRepository repository(client_);
    const auto outcome = repository.persist_parse_result(import_id_, batch);

    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;

    // 5.0：解析结果落关系表，parsed_result_json 里一个解析字段都不该有。
    const auto stored = client_->execSqlSync(
        "select g.status, g.match_method, g.inventory_revision_id::text as revision_id,"
        " g.source_component_name, g.source_component_number, g.normalized_component_number,"
        " g.version,"
        " t.bridge_component_id::text as target_component_id, t.target_role,"
        " i.instance_order, i.instance_status, i.is_photo_owner,"
        " i.component_resolution_version,"
        " m.source_candidate_id,"
        " ir.parsed_result_json#>>'{defects,0,component_number}' as component_number,"
        " ir.parsed_result_json#>>'{defects,0,component_name}' as component_name,"
        " ir.parsed_result_json#>>'{defects,0,bridge_component_id}' as leaked_component_id,"
        " iy.component_inventory_revision_id::text as year_revision_id "
        "from import_records ir "
        "join inspection_years iy on iy.id=ir.inspection_year_id "
        "join import_component_resolution_groups g on g.import_record_id=ir.id "
        "join import_component_group_members m on m.group_id=g.id "
        "join import_component_resolution_targets t on t.group_id=g.id "
        "join import_resolved_defect_instances i on i.group_member_id=m.id "
        "where ir.id=$1::uuid",
        import_id_);
    ASSERT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored[0]["status"].as<std::string>(), "bound");
    EXPECT_EQ(stored[0]["match_method"].as<std::string>(), "exact");
    EXPECT_EQ(stored[0]["revision_id"].as<std::string>(), revision_id);
    EXPECT_EQ(stored[0]["year_revision_id"].as<std::string>(), revision_id);
    EXPECT_EQ(stored[0]["target_component_id"].as<std::string>(), component_id);
    EXPECT_EQ(stored[0]["target_role"].as<std::string>(), "primary");
    EXPECT_EQ(stored[0]["source_candidate_id"].as<std::string>(), "defect_0001");
    EXPECT_EQ(stored[0]["instance_order"].as<int>(), 1);
    EXPECT_EQ(stored[0]["instance_status"].as<std::string>(), "active");
    // 唯一目标 → 这条实例就是唯一的活动实例，照片天然归它。
    EXPECT_TRUE(stored[0]["is_photo_owner"].as<bool>());
    EXPECT_EQ(stored[0]["component_resolution_version"].as<int>(),
              stored[0]["version"].as<int>());

    // 组身份：部件名称原文 + 权威归一化编号（去尾部 # 并转小写，不剥类型词）。
    EXPECT_EQ(stored[0]["source_component_name"].as<std::string>(), "上部承重构件");
    EXPECT_EQ(stored[0]["source_component_number"].as<std::string>(), "1-1#梁");
    EXPECT_EQ(stored[0]["normalized_component_number"].as<std::string>(), "1-1#梁");

    // 导入保真：构件编号/部件名称按报告原文存储，不裁剪类型词、不归一化。
    EXPECT_EQ(stored[0]["component_number"].as<std::string>(), "1-1#梁");
    EXPECT_EQ(stored[0]["component_name"].as<std::string>(), "上部承重构件");
    EXPECT_TRUE(stored[0]["leaked_component_id"].isNull())
        << "构件解析结果不得写回 parsed_result_json";
}

// 桥上同时有已确认版本和草稿时，导入必须按已确认版本匹配。此前构件匹配走
// get_latest_revision()（草稿优先），评定树匹配又另查一次年度、再解析一次——
// 两处不但可能都取到草稿，还可能彼此取到不同版本。
TEST_F(WordImportRepositoryTest, MatchesAgainstTheConfirmedRevisionWhileADraftExists) {
    const auto user_id = client_->execSqlSync(
        "select id::text from users where username='admin'")[0]["id"].as<std::string>();
    const auto component_id = client_->execSqlSync(
        "insert into bridge_components(bridge_id,structure_part,component_type,business_component_code,"
        "normalized_component_key,current_status,creation_source) values($1::uuid,'上部结构','主梁',"
        "'1-1#','word-draft-1','已确认','人工录入') returning id::text",
        bridge_id_)[0]["id"].as<std::string>();
    package_id_ = client_->execSqlSync(
        "insert into standard_packages(standard_family,standard_id,standard_code,standard_name,"
        "official_edition,package_version,contract_version,algorithm_id,effective_date,content_checksum) "
        "values('technical_condition','WORD-DRAFT-'||gen_random_uuid()::text,'WORD DRAFT',"
        "'Word草稿并存测试规范','2026','1.0.0',1,'word-draft','2026-01-01',"
        "'sha256:'||repeat('c',64)) returning id::text")[0]["id"].as<std::string>();
    const auto confirmed_id = client_->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,created_by_user_id) "
        "values($1::uuid,1,$2::uuid) returning id::text",
        bridge_id_, user_id)[0]["id"].as<std::string>();
    // 映射不能省：匹配器只认"启用 + 有生效映射"的条目，缺了它这条测试就只能证明
    // "版本号记对了"，证不了"确实按已确认版本匹上了"。
    const auto confirmed_entry_id = client_->execSqlSync(
        "insert into bridge_component_inventory_entries(inventory_revision_id,bridge_component_id,"
        "component_number,site_name,site_component_type,sort_order) "
        "values($1::uuid,$2::uuid,'1-1#梁','空心板','空心板',1) returning id::text",
        confirmed_id, component_id)[0]["id"].as<std::string>();
    client_->execSqlSync(
        "insert into bridge_component_standard_mappings(inventory_entry_id,standard_package_id,"
        "standard_bridge_type_id,standard_component_category_id,structure_part,mapping_source,"
        "confirmation_status,confirmed_by_user_id,confirmed_at) "
        "values($1::uuid,$2::uuid,'h21.bridge_type.beam','h21.component.beam.upper_bearing',"
        "'superstructure','规范模板','已确认',$3::uuid,now())",
        confirmed_entry_id, package_id_, user_id);
    client_->execSqlSync(
        "update bridge_component_inventory_revisions set status='已确认',"
        "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
        confirmed_id, user_id);
    // 草稿版本号更大；草稿优先的排序会挑中它，而它里面没有 1-1#梁。
    client_->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,"
        "baseline_revision_id,created_by_user_id) values($1::uuid,2,$2::uuid,$3::uuid)",
        bridge_id_, confirmed_id, user_id);

    bridge_report::archive::ArchivedPhotoBatch batch;
    batch.data["contract"]["parser_name"] = "liaoning-word-importer";
    batch.data["contract"]["parser_version"] = "2.0.0";
    batch.data["photos"] = Json::Value(Json::arrayValue);
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "defect_0001";
    defect["component_name"] = "上部承重构件";
    defect["component_number"] = "1-1#梁";
    defect["warnings"] = Json::Value(Json::arrayValue);
    batch.data["defects"].append(defect);

    bridge_report::db::WordImportRepository repository(client_);
    const auto outcome = repository.persist_parse_result(import_id_, batch);
    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;

    const auto stored = client_->execSqlSync(
        "select g.inventory_revision_id::text as revision_id, g.status,"
        "iy.component_inventory_revision_id::text as year_revision_id "
        "from import_records ir join inspection_years iy on iy.id=ir.inspection_year_id "
        "join import_component_resolution_groups g on g.import_record_id=ir.id "
        "where ir.id=$1::uuid", import_id_);
    ASSERT_EQ(stored.size(), 1u);
    // 组钉住的版本、年度锁定的版本，都必须是已确认那个。
    EXPECT_EQ(stored[0]["revision_id"].as<std::string>(), confirmed_id);
    EXPECT_EQ(stored[0]["year_revision_id"].as<std::string>(), confirmed_id);
    EXPECT_EQ(stored[0]["status"].as<std::string>(), "bound");
}

// 桥上根本没有已确认台账时是**降级**而不是失败：解析结果照常入库，组建出来停在
// unresolved 且不钉版本。把这条改成导入失败是行为回归——它会把"绑定面板不可用"这个
// 局部限制升级成整条导入不可校对，而现在允许先导入、之后再补台账（设计 §10）。
TEST_F(WordImportRepositoryTest, KeepsDegradingWhenTheBridgeHasNoConfirmedInventory) {
    bridge_report::archive::ArchivedPhotoBatch batch;
    batch.data["contract"]["parser_name"] = "liaoning-word-importer";
    batch.data["contract"]["parser_version"] = "2.0.0";
    batch.data["photos"] = Json::Value(Json::arrayValue);
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "defect_0001";
    defect["component_name"] = "上部承重构件";
    defect["component_number"] = "1-1#梁";
    defect["warnings"] = Json::Value(Json::arrayValue);
    batch.data["defects"].append(defect);

    bridge_report::db::WordImportRepository repository(client_);
    const auto outcome = repository.persist_parse_result(import_id_, batch);
    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;

    const auto stored = client_->execSqlSync(
        "select g.status, g.match_method, g.inventory_revision_id::text as revision_id,"
        " g.normalized_component_number,"
        " (select count(*) from import_component_group_members m where m.group_id=g.id) as members,"
        " (select count(*) from import_component_resolution_targets t where t.group_id=g.id) as targets,"
        " iy.component_inventory_revision_id::text as year_revision_id "
        "from import_records ir join inspection_years iy on iy.id=ir.inspection_year_id "
        "join import_component_resolution_groups g on g.import_record_id=ir.id "
        "where ir.id=$1::uuid", import_id_);
    ASSERT_EQ(stored.size(), 1u) << "台账未确认也要建组，否则整条导入没有构件行可校对";
    EXPECT_EQ(stored[0]["status"].as<std::string>(), "unresolved");
    EXPECT_TRUE(stored[0]["match_method"].isNull());
    EXPECT_TRUE(stored[0]["revision_id"].isNull());
    EXPECT_EQ(stored[0]["normalized_component_number"].as<std::string>(), "1-1#梁");
    EXPECT_EQ(stored[0]["members"].as<long long>(), 1);
    EXPECT_EQ(stored[0]["targets"].as<long long>(), 0);
    // 没有可锁的版本，年度也不该被锁上。
    EXPECT_TRUE(stored[0]["year_revision_id"].isNull());

    // 导入照常进入可校对状态——这正是这条测试要守住的东西。
    const auto status = client_->execSqlSync(
        "select import_status from import_records where id=$1::uuid", import_id_);
    EXPECT_EQ(status[0]["import_status"].as<std::string>(), "待校对");
}

// 同一部件下的多条无编号病害必须进同一个组。归一化写 NULL 时唯一约束会静默失效，
// 每条各成一组，"同一构件只绑一次"当场垮掉——这条专门盯住空串那个约定。
TEST_F(WordImportRepositoryTest, GroupsUnnumberedDefectsOfOnePartTogether) {
    bridge_report::archive::ArchivedPhotoBatch batch;
    batch.data["contract"]["parser_name"] = "liaoning-word-importer";
    batch.data["contract"]["parser_version"] = "2.0.0";
    batch.data["photos"] = Json::Value(Json::arrayValue);
    for (const auto* candidate : {"defect_0001", "defect_0002"}) {
        Json::Value defect(Json::objectValue);
        defect["candidate_id"] = candidate;
        defect["component_name"] = "桥面铺装";
        defect["component_number"] = Json::Value();  // 报告里本列为空
        defect["warnings"] = Json::Value(Json::arrayValue);
        batch.data["defects"].append(defect);
    }

    bridge_report::db::WordImportRepository repository(client_);
    const auto outcome = repository.persist_parse_result(import_id_, batch);
    ASSERT_TRUE(outcome.success) << outcome.error_code << ": " << outcome.error_message;

    const auto stored = client_->execSqlSync(
        "select g.normalized_component_number, g.source_component_number,"
        " count(m.id) as members "
        "from import_component_resolution_groups g "
        "join import_component_group_members m on m.group_id=g.id "
        "where g.import_record_id=$1::uuid group by g.id", import_id_);
    ASSERT_EQ(stored.size(), 1u) << "无编号病害各成一组说明归一化落了 NULL";
    EXPECT_EQ(stored[0]["normalized_component_number"].as<std::string>(), "");
    EXPECT_TRUE(stored[0]["source_component_number"].isNull());
    EXPECT_EQ(stored[0]["members"].as<long long>(), 2);
}

TEST_F(WordImportRepositoryTest, LoadsTemporaryCurrentAnnualWordContext) {
    bridge_report::db::WordImportRepository repository(client_);
    client_->execSqlSync("update import_records set import_status='已上传' where id=$1::uuid", import_id_);
    client_->execSqlSync(
        "update import_source_files set status='待解析',parsing_started_at=null where import_record_id=$1::uuid",
        import_id_);

    const auto loaded = repository.load_context(import_id_, archive_root_);

    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->bridge_id, bridge_id_);
    EXPECT_EQ(loaded->inspection_year_id, year_id_);
    EXPECT_EQ(loaded->inspection_year, 2026);
    EXPECT_EQ(loaded->source_file_system_number.substr(0, 5), "LSWJ-");
    EXPECT_EQ(loaded->source_relative_path, source_relative_path_);
    EXPECT_EQ(loaded->word_path, archive_root_ / source_relative_path_);
}

TEST_F(WordImportRepositoryTest, ParsingOnlyStartsFromUploadedOrFailed) {
    bridge_report::db::WordImportRepository repository(client_);

    client_->execSqlSync("update import_records set import_status='已上传' where id=$1::uuid", import_id_);
    client_->execSqlSync(
        "update import_source_files set status='待解析',expires_at=null where import_record_id=$1::uuid",
        import_id_);
    EXPECT_TRUE(repository.mark_parsing(import_id_));
    EXPECT_FALSE(repository.mark_parsing(import_id_));

    client_->execSqlSync("update import_records set import_status='待校对' where id=$1::uuid", import_id_);
    client_->execSqlSync(
        "update import_source_files set status='待解析' where import_record_id=$1::uuid", import_id_);
    EXPECT_FALSE(repository.mark_parsing(import_id_));

    client_->execSqlSync("update import_records set import_status='解析失败' where id=$1::uuid", import_id_);
    client_->execSqlSync(
        "update import_source_files set status='解析失败',expires_at=now()+interval '24 hours' "
        "where import_record_id=$1::uuid", import_id_);
    EXPECT_TRUE(repository.mark_parsing(import_id_));
}

TEST_F(WordImportRepositoryTest, ParsingRegistersAndClearsControlledWorkDirectory) {
    bridge_report::db::WordImportRepository repository(client_);
    client_->execSqlSync("update import_records set import_status='已上传' where id=$1::uuid", import_id_);
    client_->execSqlSync(
        "update import_source_files set status='待解析',parsing_started_at=null where import_record_id=$1::uuid",
        import_id_);
    const auto relative = std::filesystem::path("work") / "word-import" / (import_id_ + "-abcdef12");

    ASSERT_TRUE(repository.mark_parsing(import_id_, relative));
    const auto registered = client_->execSqlSync(
        "select active_parse_work_relative_path from import_source_files where import_record_id=$1::uuid",
        import_id_);
    ASSERT_EQ(registered.size(), 1u);
    EXPECT_EQ(registered[0]["active_parse_work_relative_path"].as<std::string>(), relative.generic_string());

    repository.clear_active_parse_work_path(import_id_);
    const auto cleared = client_->execSqlSync(
        "select active_parse_work_relative_path is null as cleared from import_source_files where import_record_id=$1::uuid",
        import_id_);
    ASSERT_EQ(cleared.size(), 1u);
    EXPECT_TRUE(cleared[0]["cleared"].as<bool>());
}

TEST_F(WordImportRepositoryTest, LateParseResultReportsDeletedRecordWithoutRecreatingIt) {
    bridge_report::db::WordImportRepository repository(client_);
    client_->execSqlSync("delete from import_records where id=$1::uuid", import_id_);
    bridge_report::archive::ArchivedPhotoBatch batch;
    batch.data["contract"]["parser_name"] = "liaoning-word-importer";

    const auto outcome = repository.persist_parse_result(import_id_, batch);

    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "import_record_deleted");
    EXPECT_TRUE(client_->execSqlSync("select 1 from import_records where id=$1::uuid", import_id_).empty());
}

TEST_F(WordImportRepositoryTest, ParseFailureKeepsSourceForConfiguredRetention) {
    bridge_report::db::WordImportRepository repository(client_);

    repository.mark_parse_failed(import_id_, "测试解析失败", 24);

    const auto rows = client_->execSqlSync(
        "select sf.status,sf.last_error,sf.expires_at>now()+interval '23 hours' as retained,ir.import_status "
        "from import_source_files sf join import_records ir on ir.id=sf.import_record_id "
        "where sf.import_record_id=$1::uuid", import_id_);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0]["status"].as<std::string>(), "解析失败");
    EXPECT_EQ(rows[0]["last_error"].as<std::string>(), "测试解析失败");
    EXPECT_TRUE(rows[0]["retained"].as<bool>());
    EXPECT_EQ(rows[0]["import_status"].as<std::string>(), "解析失败");
}

TEST_F(WordImportRepositoryTest, DiscardsFailedImportAndReturnsOwnedArtifactPaths) {
    const auto archived = client_->execSqlSync(
        "insert into archived_files(bridge_id,inspection_year_id,original_file_name,current_file_name,"
        "storage_relative_path,file_type,file_purpose,file_extension,file_size_bytes,file_hash) "
        "values($1::uuid,$2::uuid,'old.jpg','old.jpg','tests/old.jpg','图片','Word病害照片','.jpg',8,$3) "
        "returning id::text",
        bridge_id_, year_id_, std::string(64, 'b'));
    const auto archived_id = archived[0]["id"].as<std::string>();
    client_->execSqlSync(
        "insert into import_record_files(import_record_id,archived_file_id,file_role,process_status) "
        "values($1::uuid,$2::uuid,'附件','处理成功')",
        import_id_, archived_id);
    const auto parse_work_path = "work/word-import/" + import_id_ + "-abcdef12";
    client_->execSqlSync(
        "update import_source_files set active_parse_work_relative_path=$2 "
        "where import_record_id=$1::uuid",
        import_id_, parse_work_path);

    bridge_report::db::WordImportRepository repository(client_);
    const auto outcome = repository.discard_failed_import(import_id_);

    ASSERT_TRUE(outcome.deleted) << outcome.error_message;
    ASSERT_EQ(outcome.temporary_source_paths.size(), 1u);
    EXPECT_EQ(outcome.temporary_source_paths[0], source_relative_path_);
    ASSERT_EQ(outcome.archived_file_paths.size(), 1u);
    EXPECT_EQ(outcome.archived_file_paths[0], "tests/old.jpg");
    ASSERT_EQ(outcome.parse_work_paths.size(), 1u);
    EXPECT_EQ(outcome.parse_work_paths[0], parse_work_path);
    EXPECT_TRUE(client_->execSqlSync(
        "select 1 from import_records where id=$1::uuid", import_id_).empty());
    EXPECT_TRUE(client_->execSqlSync(
        "select 1 from import_source_files where import_record_id=$1::uuid", import_id_).empty());
    EXPECT_TRUE(client_->execSqlSync(
        "select 1 from archived_files where id=$1::uuid", archived_id).empty());
}

TEST_F(WordImportRepositoryTest, NonCurrentAnnualWordCannotBeLoadedForParsing) {
    bridge_report::db::WordImportRepository repository(client_);
    client_->execSqlSync("update inspection_years set is_current = false where id = $1::uuid", year_id_);

    EXPECT_FALSE(repository.load_context(import_id_, archive_root_).has_value());
}

}  // namespace

// 抢不到年度版本时必须整体回滚。这条路径的全部价值就在于"出错时别毁数据"：
// 解析已经成功、照片已经归档，只是没抢到版本；此时若继续提交，病害会按一个版本
// 落盘而年度指向另一个版本。路由那半（不走 discard_failed_import_safely、改判
// 解析失败以便重试）在 WordImportRoutes 里，需要 HTTP 级夹具，尚未覆盖。
TEST_F(WordImportRepositoryTest, RollsBackEverythingWhenTheYearRevisionCannotBeLocked) {
    const auto user_id = client_->execSqlSync(
        "select id::text from users where username='admin'")[0]["id"].as<std::string>();
    const auto confirmed_id = client_->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,created_by_user_id) "
        "values($1::uuid,1,$2::uuid) returning id::text",
        bridge_id_, user_id)[0]["id"].as<std::string>();
    client_->execSqlSync(
        "update bridge_component_inventory_revisions set status='已确认',"
        "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
        confirmed_id, user_id);
    // 年度版本仍为空（所以会走锁定分支），但年度已不在"待校对"——
    // lock_pending_year_revision 的 UPDATE 带 status='待校对' 谓词，命中 0 行，
    // 回读又发现仍是空，只能判定抢锁失败。这代表任何一种"读到未锁定、下手时锁不上"
    // 的交错，包括共享同一年度的另一条导入记录抢先一步。
    client_->execSqlSync(
        "update inspection_years set status='已确认' where id=$1::uuid", year_id_);

    bridge_report::archive::ArchivedPhotoBatch batch;
    batch.data["contract"]["parser_name"] = "liaoning-word-importer";
    batch.data["contract"]["parser_version"] = "2.0.0";
    batch.data["photos"] = Json::Value(Json::arrayValue);
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "defect_0001";
    defect["component_name"] = "上部承重构件";
    defect["component_number"] = "1-1#梁";
    defect["warnings"] = Json::Value(Json::arrayValue);
    batch.data["defects"].append(defect);

    bridge_report::db::WordImportRepository repository(client_);
    const auto outcome = repository.persist_parse_result(import_id_, batch);

    ASSERT_FALSE(outcome.success);
    EXPECT_EQ(outcome.error_code, "component_inventory_revision_changed");
    // 专用错误码，路由据此把这次失败排除在删除路径之外；混进通用失败码会让
    // 一次可重试的冲突变成"导入记录、原始 Word 与归档一起被删"。
    EXPECT_NE(outcome.error_code, "db_write_failed");

    const auto after = client_->execSqlSync(
        "select ir.import_status, ir.parsed_result_json::text as parsed, "
        "iy.component_inventory_revision_id::text as year_revision_id, "
        "(select count(*) from import_record_files f where f.import_record_id=ir.id "
        " and f.file_role='附件') as attachment_count "
        "from import_records ir join inspection_years iy on iy.id=ir.inspection_year_id "
        "where ir.id=$1::uuid", import_id_);
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after[0]["import_status"].as<std::string>(), "解析中")
        << "回滚后状态不该被改写；转成解析失败是路由的事，以便下次 mark_parsing 能重入";
    EXPECT_TRUE(after[0]["year_revision_id"].isNull()) << "抢锁失败不得留下半截锁定";
    EXPECT_EQ(after[0]["attachment_count"].as<long long>(), 0)
        << "照片关系必须随事务一起回滚";
    EXPECT_EQ(after[0]["parsed"].as<std::string>().find("defect_0001"), std::string::npos)
        << "解析结果不得落盘";
}

// 版本冲突之后必须真的能重来一次。这条把冲突分支的三步串起来跑：
// 抢锁失败 -> 清理本次的解析工作区 -> 转"解析失败" -> 重新 mark_parsing。
//
// 单看每一步都有测试，但"能不能重试"是它们串起来才成立的性质：只要有一步顺序错了
// （比如没转解析失败就收工），记录就会永远卡在"解析中"，用户只能重新上传。
TEST_F(WordImportRepositoryTest, AVersionRaceLeavesTheImportRetryable) {
    const auto user_id = client_->execSqlSync(
        "select id::text from users where username='admin'")[0]["id"].as<std::string>();
    const auto confirmed_id = client_->execSqlSync(
        "insert into bridge_component_inventory_revisions(bridge_id,revision_number,created_by_user_id) "
        "values($1::uuid,1,$2::uuid) returning id::text",
        bridge_id_, user_id)[0]["id"].as<std::string>();
    client_->execSqlSync(
        "update bridge_component_inventory_revisions set status='已确认',"
        "confirmed_by_user_id=$2::uuid,confirmed_at=now() where id=$1::uuid",
        confirmed_id, user_id);
    // 年度版本仍为空但年度已不在"待校对"：lock_pending_year_revision 的 UPDATE 命中
    // 0 行、回读也仍是空，判定抢锁失败——代表任何一种"读到未锁定、下手时锁不上"的交错。
    client_->execSqlSync(
        "update inspection_years set status='已确认' where id=$1::uuid", year_id_);
    // 解析工作区路径挂在来源文件行上，不在导入记录上。
    client_->execSqlSync(
        "update import_source_files set active_parse_work_relative_path='work/word-import/00000000-0000-0000-0000-000000000001-abcdef' "
        "where import_record_id=$1::uuid", import_id_);

    bridge_report::archive::ArchivedPhotoBatch batch;
    batch.data["contract"]["parser_name"] = "liaoning-word-importer";
    batch.data["contract"]["parser_version"] = "2.0.0";
    batch.data["photos"] = Json::Value(Json::arrayValue);
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "defect_0001";
    defect["component_name"] = "上部承重构件";
    defect["component_number"] = "1-1#梁";
    defect["warnings"] = Json::Value(Json::arrayValue);
    batch.data["defects"].append(defect);

    bridge_report::db::WordImportRepository repository(client_);
    const auto outcome = repository.persist_parse_result(import_id_, batch);
    ASSERT_FALSE(outcome.success);
    ASSERT_EQ(outcome.error_code, "component_inventory_revision_changed");

    // 路由冲突分支做的两件事：清工作区、转解析失败。绝不调 discard_failed_import。
    repository.clear_active_parse_work_path(import_id_);
    repository.mark_parse_failed(import_id_, outcome.error_message, 24);

    const auto after = client_->execSqlSync(
        "select ir.import_status, sf.active_parse_work_relative_path, sf.status as source_status "
        "from import_records ir join import_source_files sf on sf.import_record_id=ir.id "
        "where ir.id=$1::uuid", import_id_);
    ASSERT_EQ(after.size(), 1u) << "导入记录与来源文件都必须还在，没被删掉";
    EXPECT_EQ(after[0]["import_status"].as<std::string>(), "解析失败");
    EXPECT_EQ(after[0]["source_status"].as<std::string>(), "解析失败");
    EXPECT_TRUE(after[0]["active_parse_work_relative_path"].isNull())
        << "本次的解析工作区路径必须清掉，否则下次解析会挂在旧目录上";

    // 关键一条：能重新进入解析。mark_parsing() 只接受"已上传"/"解析失败"，
    // 冲突分支若只是跳过删除、不转状态，这里就会永远失败。
    EXPECT_TRUE(repository.mark_parsing(import_id_)) << "版本冲突之后必须能重试";
}
