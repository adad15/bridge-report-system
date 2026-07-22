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
    const auto stored = client_->execSqlSync(
        "select parsed_result_json#>>'{defects,0,bridge_component_id}' as component_id,"
        "parsed_result_json#>>'{defects,0,standard_component_category_id}' as category_id,"
        "parsed_result_json#>>'{defects,0,resolved_structure_part}' as structure_part,"
        "parsed_result_json#>>'{defects,0,component_match_method}' as match_method,"
        "parsed_result_json#>>'{defects,0,component_inventory_revision_id}' as revision_id,"
        "parsed_result_json#>>'{defects,0,component_number}' as component_number,"
        "parsed_result_json#>>'{defects,0,component_name}' as component_name "
        "from import_records where id=$1::uuid",
        import_id_);
    ASSERT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored[0]["component_id"].as<std::string>(), component_id);
    EXPECT_EQ(stored[0]["category_id"].as<std::string>(), "h21.component.beam.upper_bearing");
    EXPECT_EQ(stored[0]["structure_part"].as<std::string>(), "上部结构");
    EXPECT_EQ(stored[0]["match_method"].as<std::string>(), "exact");
    EXPECT_EQ(stored[0]["revision_id"].as<std::string>(), revision_id);
    // 导入保真：构件编号/部件名称按报告原文存储，不裁剪类型词、不归一化。
    EXPECT_EQ(stored[0]["component_number"].as<std::string>(), "1-1#梁");
    EXPECT_EQ(stored[0]["component_name"].as<std::string>(), "上部承重构件");

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

TEST_F(WordImportRepositoryTest, NonCurrentAnnualWordCannotBeLoadedForParsing) {
    bridge_report::db::WordImportRepository repository(client_);
    client_->execSqlSync("update inspection_years set is_current = false where id = $1::uuid", year_id_);

    EXPECT_FALSE(repository.load_context(import_id_, archive_root_).has_value());
}

}  // namespace
