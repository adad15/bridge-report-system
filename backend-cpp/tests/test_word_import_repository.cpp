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
        const auto main_file = client_->execSqlSync(
            "insert into archived_files (bridge_id, inspection_year_id, original_file_name, current_file_name, "
            "storage_relative_path, file_type, file_purpose, file_extension) "
            "values ($1::uuid, $2::uuid, 'report.docx', 'report.docx', 'tests/report.docx', "
            "'Word文档', '导入主报告', '.docx') returning id", bridge_id_, year_id_);
        main_file_id_ = main_file[0]["id"].as<std::string>();
        const auto record = client_->execSqlSync(
            "insert into import_records (bridge_id, inspection_year_id, import_name, source_type, import_status, main_file_id) "
            "values ($1::uuid, $2::uuid, 'Word导入事务测试', '软件导出Word', '解析中', $3::uuid) returning id",
            bridge_id_, year_id_, main_file_id_);
        import_id_ = record[0]["id"].as<std::string>();
        archive_root_ = std::filesystem::temp_directory_path() / ("bridge-word-parse-" + import_id_);
        std::filesystem::create_directories(archive_root_ / "tests");
        std::ofstream(archive_root_ / "tests" / "report.docx", std::ios::binary) << "fake-docx";
    }

    void TearDown() override {
        if (!client_) return;
        client_->execSqlSync("delete from import_records where id = $1::uuid", import_id_);
        client_->execSqlSync("delete from archived_files where bridge_id = $1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where id = $1::uuid", year_id_);
        client_->execSqlSync("delete from bridges where id = $1::uuid", bridge_id_);
        client_->closeAll();
        std::filesystem::remove_all(archive_root_);
    }

    drogon::orm::DbClientPtr client_;
    std::string bridge_id_;
    std::string year_id_;
    std::string main_file_id_;
    std::string import_id_;
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
}

TEST_F(WordImportRepositoryTest, LoadsArchivedCurrentAnnualWordContext) {
    bridge_report::db::WordImportRepository repository(client_);

    const auto loaded = repository.load_context(import_id_, archive_root_);

    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->bridge_id, bridge_id_);
    EXPECT_EQ(loaded->inspection_year_id, year_id_);
    EXPECT_EQ(loaded->inspection_year, 2026);
    EXPECT_EQ(loaded->word_path, archive_root_ / "tests" / "report.docx");
}

TEST_F(WordImportRepositoryTest, ParsingOnlyStartsFromUploadedOrFailed) {
    bridge_report::db::WordImportRepository repository(client_);

    client_->execSqlSync("update import_records set import_status = '已上传' where id = $1::uuid", import_id_);
    EXPECT_TRUE(repository.mark_parsing(import_id_));
    EXPECT_FALSE(repository.mark_parsing(import_id_));

    client_->execSqlSync("update import_records set import_status = '待校对' where id = $1::uuid", import_id_);
    EXPECT_FALSE(repository.mark_parsing(import_id_));

    client_->execSqlSync("update import_records set import_status = '解析失败' where id = $1::uuid", import_id_);
    EXPECT_TRUE(repository.mark_parsing(import_id_));
}

TEST_F(WordImportRepositoryTest, NonCurrentAnnualWordCannotBeLoadedForParsing) {
    bridge_report::db::WordImportRepository repository(client_);
    client_->execSqlSync("update inspection_years set is_current = false where id = $1::uuid", year_id_);

    EXPECT_FALSE(repository.load_context(import_id_, archive_root_).has_value());
}

}  // namespace
