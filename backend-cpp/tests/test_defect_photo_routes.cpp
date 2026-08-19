#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/archive/ExtractedPhotoArchive.hpp"
#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/db/DbClientFactory.hpp"
#include "bridge_report/http/DefectPhotoRoutes.hpp"

namespace {

using bridge_report::http::UploadedPhotoNaming;
using bridge_report::http::build_uploaded_photo_candidate;
using bridge_report::http::delete_uploaded_photo;
using bridge_report::http::draft_has_defect_candidate;
using bridge_report::http::draft_has_photo_naming;
using bridge_report::http::insert_uploaded_photo;
using bridge_report::http::next_uploaded_photo_naming;
using bridge_report::http::take_photo_candidate;

Json::Value photo(const std::string& candidate_id, const std::string& photo_number,
                  const std::string& source_type = "word") {
    Json::Value value(Json::objectValue);
    value["candidate_id"] = candidate_id;
    value["photo_number"] = photo_number;
    value["source_ref"]["source_type"] = source_type;
    return value;
}

Json::Value draft_with(const Json::Value& photos) {
    Json::Value draft(Json::objectValue);
    draft["photos"] = photos;
    draft["defects"] = Json::Value(Json::arrayValue);
    Json::Value defect(Json::objectValue);
    defect["candidate_id"] = "defect_0001";
    draft["defects"].append(defect);
    return draft;
}

TEST(DefectPhotoNamingTest, StartsAtOneWhenTheImportHasNoManualPhoto) {
    const auto naming = next_uploaded_photo_naming(draft_with(Json::Value(Json::arrayValue)));

    EXPECT_EQ(naming.candidate_id, "manual_photo_0001");
    EXPECT_EQ(naming.photo_number, "补-1");
}

// Word 的 2.1-5 不是人工编号，不能把序号顶上去。
TEST(DefectPhotoNamingTest, IgnoresWordPhotoNumbersWhenAllocating) {
    Json::Value photos(Json::arrayValue);
    photos.append(photo("photo_0001", "2.1-5"));
    photos.append(photo("photo_0002", "12-3"));

    const auto naming = next_uploaded_photo_naming(draft_with(photos));

    EXPECT_EQ(naming.candidate_id, "manual_photo_0001");
    EXPECT_EQ(naming.photo_number, "补-1");
}

TEST(DefectPhotoNamingTest, ContinuesAfterTheHighestManualNumberInTheImport) {
    Json::Value photos(Json::arrayValue);
    photos.append(photo("photo_0001", "2.1-5"));
    photos.append(photo("manual_photo_0001", "补-1", "manual"));
    photos.append(photo("manual_photo_0007", "补-9", "manual"));
    photos.append(photo("manual_photo_0003", "补-2", "manual"));

    const auto naming = next_uploaded_photo_naming(draft_with(photos));

    EXPECT_EQ(naming.candidate_id, "manual_photo_0008");
    EXPECT_EQ(naming.photo_number, "补-10");
    EXPECT_FALSE(draft_has_photo_naming(draft_with(photos), naming));
}

TEST(DefectPhotoCandidateTest, BuildsAContractFourPhotoWithoutReviewState) {
    const auto candidate = build_uploaded_photo_candidate(
        UploadedPhotoNaming{"manual_photo_0001", "补-1"}, "defect_0042", "IMG_2031.jpg",
        "梁底裂缝补拍", "bridges/QL-1_桥/2026/imports/DRJL-1_导入/photos/x.jpg");

    EXPECT_EQ(candidate["candidate_id"].asString(), "manual_photo_0001");
    EXPECT_EQ(candidate["photo_number"].asString(), "补-1");
    EXPECT_EQ(candidate["linked_defect_candidate_id"].asString(), "defect_0042");
    EXPECT_FALSE(candidate.isMember("match_status"));
    EXPECT_FALSE(candidate.isMember("review_status"));
    EXPECT_EQ(candidate["source_ref"]["source_type"].asString(), "manual");
    EXPECT_DOUBLE_EQ(candidate["confidence"].asDouble(), 1.0);
    // 图注即最终报告里的照片题注。
    EXPECT_EQ(candidate["extracted_file"]["original_caption"].asString(), "梁底裂缝补拍");
    EXPECT_EQ(candidate["extracted_file"]["temporary_file_name"].asString(), "IMG_2031.jpg");
    EXPECT_TRUE(candidate["warnings"].isArray());
}

TEST(DefectPhotoCandidateTest, KeepsAnEmptyCaptionNullRatherThanBlank) {
    const auto candidate = build_uploaded_photo_candidate(
        UploadedPhotoNaming{"manual_photo_0001", "补-1"}, "defect_0042", "a.jpg", "", "photos/x.jpg");

    EXPECT_TRUE(candidate["extracted_file"]["original_caption"].isNull());
}

TEST(DefectPhotoDraftTest, FindsOnlyDefectsThatBelongToTheImport) {
    const auto draft = draft_with(Json::Value(Json::arrayValue));

    EXPECT_TRUE(draft_has_defect_candidate(draft, "defect_0001"));
    EXPECT_FALSE(draft_has_defect_candidate(draft, "defect_9999"));
}

TEST(DefectPhotoDraftTest, TakesTheNamedCandidateAndLeavesTheRestAlone) {
    Json::Value photos(Json::arrayValue);
    photos.append(photo("photo_0001", "2.1-1"));
    photos.append(photo("manual_photo_0001", "补-1", "manual"));
    auto draft = draft_with(photos);

    const auto removed = take_photo_candidate(draft, "manual_photo_0001");

    ASSERT_FALSE(removed.isNull());
    EXPECT_EQ(removed["source_ref"]["source_type"].asString(), "manual");
    ASSERT_EQ(draft["photos"].size(), 1u);
    EXPECT_EQ(draft["photos"][0]["candidate_id"].asString(), "photo_0001");
    EXPECT_TRUE(take_photo_candidate(draft, "manual_photo_0001").isNull());
}

std::string jpeg_bytes() {
    return std::string("\xff\xd8\xff\xe0\x00\x10JFIF\x00\xff\xd9", 13);
}

/**
 * @brief 端点的库写部分：自建桥梁与导入记录，结束时只删自己建的行。
 */
class DefectPhotoWriteTest : public testing::Test {
protected:
    void SetUp() override {
        if (std::getenv("BRIDGE_REPORT_TEST_DATABASE_URL") == nullptr) {
            GTEST_SKIP() << "BRIDGE_REPORT_TEST_DATABASE_URL is not set";
        }
        client_ = bridge_report::db::create_db_client(bridge_report::config::PostgresConfig{});
        bridge_id_ = client_->execSqlSync(
            "insert into bridges(bridge_name) values('照片上传测试') returning id::text")[0]["id"]
                .as<std::string>();
        year_id_ = client_->execSqlSync(
            "insert into inspection_years(bridge_id,inspection_year,status,is_current) "
            "values($1::uuid,2026,'待校对',true) returning id::text",
            bridge_id_)[0]["id"].as<std::string>();

        Json::Value parsed(Json::objectValue);
        parsed["photos"] = Json::Value(Json::arrayValue);
        parsed["photos"].append(photo("photo_0001", "2.1-1"));
        parsed["defects"] = Json::Value(Json::arrayValue);
        Json::Value defect(Json::objectValue);
        defect["candidate_id"] = "defect_0001";
        parsed["defects"].append(defect);
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        import_id_ = client_->execSqlSync(
            "insert into import_records(bridge_id,inspection_year_id,import_name,source_type,import_status,"
            "parsed_result_json) values($1::uuid,$2::uuid,'照片上传导入','软件导出Word','待校对',$3::jsonb) "
            "returning id::text",
            bridge_id_, year_id_, Json::writeString(builder, parsed))[0]["id"].as<std::string>();

        detail_.id = import_id_;
        detail_.bridge_id = bridge_id_;
        detail_.inspection_year_id = year_id_;
        detail_.system_number = "DRJL-TEST";
        detail_.import_name = "照片上传导入";
        detail_.bridge_system_number = "QL-TEST";
        detail_.bridge_name = "照片上传测试";
        detail_.inspection_year = 2026;
        detail_.import_status = "待校对";

        archive_root_ = std::filesystem::temp_directory_path() / ("bridge-report-upload-" + import_id_);
        std::filesystem::create_directories(archive_root_);
    }

    void TearDown() override {
        if (!client_) return;
        client_->execSqlSync("delete from import_record_files where import_record_id=$1::uuid", import_id_);
        client_->execSqlSync("delete from import_records where id=$1::uuid", import_id_);
        client_->execSqlSync("delete from archived_files where bridge_id=$1::uuid", bridge_id_);
        client_->execSqlSync("delete from inspection_years where id=$1::uuid", year_id_);
        client_->execSqlSync("delete from bridges where id=$1::uuid", bridge_id_);
        client_->closeAll();
        std::error_code error;
        std::filesystem::remove_all(archive_root_, error);
    }

    bridge_report::archive::ArchivedPhotoFile archive_one(const UploadedPhotoNaming& naming) {
        bridge_report::archive::PhotoArchiveContext context;
        context.staging_root = archive_root_;
        context.archive_root = archive_root_;
        context.bridge_system_number = detail_.bridge_system_number;
        context.bridge_name = detail_.bridge_name;
        context.inspection_year = 2026;
        context.import_record_system_number = detail_.system_number;
        context.import_name = detail_.import_name;
        return bridge_report::archive::archive_uploaded_photo(
            {jpeg_bytes(), "IMG_2031.jpg", naming.candidate_id, 0}, context);
    }

    Json::Value stored_draft() {
        const auto row = client_->execSqlSync(
            "select parsed_result_json::text as draft from import_records where id=$1::uuid", import_id_);
        Json::Value value;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream(row[0]["draft"].as<std::string>());
        Json::parseFromStream(builder, stream, &value, &errors);
        return value;
    }

    drogon::orm::DbClientPtr client_;
    bridge_report::review::ImportRecordDetail detail_;
    std::string bridge_id_, year_id_, import_id_;
    std::filesystem::path archive_root_;
};

TEST_F(DefectPhotoWriteTest, AppendsTheCandidateAndRegistersBothFileRows) {
    const UploadedPhotoNaming naming{"manual_photo_0001", "补-1"};
    const auto file = archive_one(naming);
    const auto candidate = build_uploaded_photo_candidate(
        naming, "defect_0001", "IMG_2031.jpg", "补拍", file.storage_relative_path.generic_string());

    const auto written = insert_uploaded_photo(client_, detail_, file, naming, candidate);

    ASSERT_TRUE(written.success) << written.error_message;
    const auto draft = stored_draft();
    ASSERT_EQ(draft["photos"].size(), 2u);
    EXPECT_EQ(draft["photos"][1]["candidate_id"].asString(), "manual_photo_0001");
    EXPECT_FALSE(draft["photos"][1].isMember("match_status"));
    EXPECT_FALSE(draft["photos"][1].isMember("review_status"));
    // 照片必须同时进两张文件表，否则 /photos/{id}/content 取不到图。
    EXPECT_EQ(client_->execSqlSync(
        "select count(*) as n from import_record_files irf join archived_files af on af.id=irf.archived_file_id "
        "where irf.import_record_id=$1::uuid and irf.file_role='附件' and irf.process_status='处理成功' "
        "and af.file_type='图片' and af.file_purpose='人工补充照片'",
        import_id_)[0]["n"].as<int>(), 1);
}

TEST_F(DefectPhotoWriteTest, RefusesADefectThatIsNotInThisImport) {
    const UploadedPhotoNaming naming{"manual_photo_0001", "补-1"};
    const auto file = archive_one(naming);
    const auto candidate = build_uploaded_photo_candidate(
        naming, "defect_9999", "IMG_2031.jpg", "", file.storage_relative_path.generic_string());

    const auto written = insert_uploaded_photo(client_, detail_, file, naming, candidate);

    EXPECT_FALSE(written.success);
    EXPECT_EQ(written.error_code, "defect_candidate_not_found");
    EXPECT_EQ(stored_draft()["photos"].size(), 1u);
}

TEST_F(DefectPhotoWriteTest, RefusesToTouchAnImportThatIsNoLongerUnderReview) {
    client_->execSqlSync("update import_records set import_status='已确认' where id=$1::uuid", import_id_);
    const UploadedPhotoNaming naming{"manual_photo_0001", "补-1"};
    const auto file = archive_one(naming);
    const auto candidate = build_uploaded_photo_candidate(
        naming, "defect_0001", "IMG_2031.jpg", "", file.storage_relative_path.generic_string());

    const auto written = insert_uploaded_photo(client_, detail_, file, naming, candidate);

    EXPECT_FALSE(written.success);
    EXPECT_EQ(written.error_code, "import_record_not_editable");
    EXPECT_EQ(client_->execSqlSync(
        "select count(*) as n from import_record_files where import_record_id=$1::uuid",
        import_id_)[0]["n"].as<int>(), 0);
}

TEST_F(DefectPhotoWriteTest, RemovesTheDraftEntryAndBothFileRowsOnDelete) {
    const UploadedPhotoNaming naming{"manual_photo_0001", "补-1"};
    const auto file = archive_one(naming);
    const auto candidate = build_uploaded_photo_candidate(
        naming, "defect_0001", "IMG_2031.jpg", "", file.storage_relative_path.generic_string());
    ASSERT_TRUE(insert_uploaded_photo(client_, detail_, file, naming, candidate).success);

    const auto removed = delete_uploaded_photo(client_, import_id_, "manual_photo_0001");

    ASSERT_TRUE(removed.success) << removed.error_message;
    EXPECT_EQ(removed.storage_relative_path, file.storage_relative_path.generic_string());
    EXPECT_EQ(removed.sha256, file.sha256);
    EXPECT_EQ(stored_draft()["photos"].size(), 1u);
    EXPECT_EQ(client_->execSqlSync(
        "select count(*) as n from import_record_files where import_record_id=$1::uuid",
        import_id_)[0]["n"].as<int>(), 0);
    EXPECT_EQ(client_->execSqlSync(
        "select count(*) as n from archived_files where bridge_id=$1::uuid",
        bridge_id_)[0]["n"].as<int>(), 0);

    // 归档文件由调用方在事务提交后删除。
    bridge_report::archive::remove_archived_photo(archive_root_, removed.storage_relative_path, removed.sha256);
    EXPECT_FALSE(std::filesystem::exists(archive_root_ / file.storage_relative_path));
}

TEST_F(DefectPhotoWriteTest, RefusesToDeleteAPhotoExtractedFromWord) {
    const auto removed = delete_uploaded_photo(client_, import_id_, "photo_0001");

    EXPECT_FALSE(removed.success);
    EXPECT_EQ(removed.error_code, "photo_not_deletable");
    EXPECT_EQ(stored_draft()["photos"].size(), 1u);
}

TEST_F(DefectPhotoWriteTest, ReportsAMissingPhotoInsteadOfSilentlySucceeding) {
    const auto removed = delete_uploaded_photo(client_, import_id_, "manual_photo_9999");

    EXPECT_FALSE(removed.success);
    EXPECT_EQ(removed.error_code, "photo_candidate_not_found");
}

}  // namespace

// 编辑锁必须在写事务内复查。路由入口那道 require_active_edit_lock 是事务外的：
// 锁 2 分钟过期一次、还能被管理员随时强制收回，而这两个接口都会改写
// parsed_result_json。检查通过之后到照片真正落库之间锁失效时，写入必须被挡下。
TEST_F(DefectPhotoWriteTest, RefusesToInsertWhenTheEditLockIsNoLongerValid) {
    const UploadedPhotoNaming naming{"manual_photo_0001", "补-1"};
    const auto file = archive_one(naming);
    const auto candidate = build_uploaded_photo_candidate(
        naming, "defect_0001", "IMG_2031.jpg", "补拍", file.storage_relative_path.generic_string());
    // 从未签发过的令牌：代表锁已过期、被强制收回，或本来就属于别人。
    const bridge_report::db::EditLockCredentials stale{
        "00000000-0000-0000-0000-000000000001",
        "00000000-0000-0000-0000-000000000002", "never-issued-token"};

    const auto written = insert_uploaded_photo(client_, detail_, file, naming, candidate, stale);

    EXPECT_FALSE(written.success);
    EXPECT_EQ(written.error_code, "edit_lock_invalid");
    // 草稿必须原封不动：夹具里本来就一张照片。
    EXPECT_EQ(stored_draft()["photos"].size(), 1u);
    EXPECT_EQ(client_->execSqlSync(
        "select count(*) as n from import_record_files where import_record_id=$1::uuid",
        import_id_)[0]["n"].as<int>(), 0);
}

TEST_F(DefectPhotoWriteTest, RefusesToDeleteWhenTheEditLockIsNoLongerValid) {
    const UploadedPhotoNaming naming{"manual_photo_0001", "补-1"};
    const auto file = archive_one(naming);
    const auto candidate = build_uploaded_photo_candidate(
        naming, "defect_0001", "IMG_2031.jpg", "补拍", file.storage_relative_path.generic_string());
    ASSERT_TRUE(insert_uploaded_photo(client_, detail_, file, naming, candidate).success);
    const bridge_report::db::EditLockCredentials stale{
        "00000000-0000-0000-0000-000000000001",
        "00000000-0000-0000-0000-000000000002", "never-issued-token"};

    const auto removed = delete_uploaded_photo(client_, import_id_, "manual_photo_0001", stale);

    EXPECT_FALSE(removed.success);
    EXPECT_EQ(removed.error_code, "edit_lock_invalid");
    EXPECT_EQ(stored_draft()["photos"].size(), 2u) << "锁失效时照片不得被删";
}
