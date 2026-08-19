#include <filesystem>

#include <gtest/gtest.h>
#include <json/json.h>

#include "bridge_report/http/WordImportRoutes.hpp"

namespace {

bridge_report::db::WordImportContext context() {
    bridge_report::db::WordImportContext value;
    value.bridge_system_number = "QL-000001";
    value.bridge_name = "绕阳河二号桥";
    value.inspection_year = 2026;
    value.import_record_system_number = "DRJL-000001";
    value.import_name = "软件Word导入";
    value.source_type = "软件导出Word";
    value.source_file_system_number = "LSWJ-000001";
    value.source_relative_path = "883a08d4-557d-4421-8cc3-c1036e19b56a.docx";
    value.word_path = "D:/archive/report.docx";
    return value;
}

TEST(WordImportRoutesTest, BuildsPythonRequestFromTrustedDatabaseContext) {
    Json::Value body;
    body["rule_profile"] = "辽宁国省干线";
    body["import_mode"] = "已有桥年度导入";
    body["file_role"] = "当前年度检测资料";
    body["data_role"] = "当前年度";
    body["inspection_date"] = "2026-07-07";
    body["report_number"] = "TEST-001";
    body["project_name"] = "2026年度定期检测";
    body["selected_bridge_system_number"] = "伪造编号";

    const auto request = bridge_report::http::build_python_word_request(
        context(), body, "D:/staging/photos");

    EXPECT_EQ(request["docx_path"].asString(), "D:/archive/report.docx");
    EXPECT_EQ(request["selected_bridge_system_number"].asString(), "QL-000001");
    EXPECT_EQ(request["selected_bridge_name"].asString(), "绕阳河二号桥");
    EXPECT_EQ(request["import_record_system_number"].asString(), "DRJL-000001");
    EXPECT_EQ(request["archived_file_system_number"].asString(), "LSWJ-000001");
    EXPECT_EQ(request["report_number"].asString(), "TEST-001");
    EXPECT_EQ(request["temporary_photo_output_dir"].asString(), "D:/staging/photos");
}

TEST(WordImportRoutesTest, RejectsMissingRequiredBusinessChoice) {
    Json::Value body;
    body["rule_profile"] = "辽宁国省干线";
    EXPECT_THROW(
        bridge_report::http::build_python_word_request(context(), body, "D:/staging/photos"),
        std::invalid_argument
    );
}

TEST(WordImportRoutesTest, ExtractsAnnualInspectionDataFromPythonEnvelope) {
    Json::Value response;
    response["data"]["contract"]["name"] = "BridgeAnnualInspectionData";
    response["temporary_photo_files"].append("photo-1.jpg");

    const auto data = bridge_report::http::extract_python_parse_data(response);

    EXPECT_EQ(data["contract"]["name"].asString(), "BridgeAnnualInspectionData");
    EXPECT_FALSE(data.isMember("temporary_photo_files"));
}

TEST(WordImportRoutesTest, RejectsPythonEnvelopeWithoutDataObject) {
    Json::Value response;
    response["temporary_photo_files"] = Json::arrayValue;
    EXPECT_THROW(bridge_report::http::extract_python_parse_data(response), std::invalid_argument);
}

TEST(WordImportRoutesTest, ExtractsStructuredPythonBusinessError) {
    Json::Value response;
    response["detail"]["code"] = "rating_table_not_found";
    response["detail"]["message"] = "未识别到表4.1-2总体技术状况评定表。";

    const auto error = bridge_report::http::extract_python_parse_error(response);

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->code, "rating_table_not_found");
    EXPECT_EQ(error->message, "未识别到表4.1-2总体技术状况评定表。");
}

TEST(WordImportRoutesTest, RejectsMalformedPythonBusinessError) {
    Json::Value response;
    response["detail"]["code"] = "rating_table_not_found";
    response["detail"]["message"] = "";

    EXPECT_FALSE(bridge_report::http::extract_python_parse_error(response).has_value());
    EXPECT_FALSE(bridge_report::http::extract_python_parse_error(Json::Value(Json::objectValue)).has_value());
}

}  // namespace

// ---------------------------------------------------------------------------
// 接口同步：只换数据来源，其余业务上下文与 Word 那条路取自同一处。
// ---------------------------------------------------------------------------

namespace {

bridge_report::db::WordImportContext source_context() {
    auto value = context();
    value.source_type = "接口同步";
    value.source_relative_path = "883a08d4-557d-4421-8cc3-c1036e19b56a.srcref";
    value.word_path = "D:/temp/883a08d4-557d-4421-8cc3-c1036e19b56a.srcref";
    return value;
}

Json::Value source_body() {
    Json::Value body;
    body["import_mode"] = "已有桥年度导入";
    body["file_role"] = "当前年度检测资料";
    body["data_role"] = "当前年度";
    body["inspection_date"] = "2026-07-07";
    body["report_number"] = "TEST-001";
    body["project_name"] = "2026年度定期检测";
    return body;
}

}  // namespace

TEST(WordImportRoutesTest, RecognizesAnInterfaceSyncImport) {
    EXPECT_TRUE(bridge_report::http::is_source_db_import(source_context()));
    EXPECT_FALSE(bridge_report::http::is_source_db_import(context()));
}

TEST(WordImportRoutesTest, BuildsTheSourceRequestFromTheStoredReference) {
    // 路径与 taskId 来自导入记录登记时存下的引用，不由本次请求现填。
    const bridge_report::archive::SourceDbReference reference{"D:/数据/离线库.sqlite", "task-9"};

    const auto request = bridge_report::http::build_python_source_request(
        source_context(), source_body(), reference, "D:/staging/photos");

    EXPECT_EQ(request["source_db_path"].asString(), "D:/数据/离线库.sqlite");
    EXPECT_EQ(request["task_id"].asString(), "task-9");
    EXPECT_EQ(request["selected_bridge_system_number"].asString(), "QL-000001");
    EXPECT_EQ(request["import_record_system_number"].asString(), "DRJL-000001");
    EXPECT_EQ(request["temporary_photo_output_dir"].asString(), "D:/staging/photos");
    // 源库那条路没有 Word 规则档，多送一个字段会被 Python 的 extra="forbid" 打回。
    EXPECT_FALSE(request.isMember("rule_profile"));
    EXPECT_FALSE(request.isMember("docx_path"));
}

TEST(WordImportRoutesTest, StillRequiresTheBusinessChoicesForASourceImport) {
    Json::Value body;
    body["import_mode"] = "已有桥年度导入";

    EXPECT_THROW(
        bridge_report::http::build_python_source_request(
            source_context(), body, {"D:/db.sqlite", "task-9"}, "D:/staging/photos"),
        std::invalid_argument
    );
}

// 这条判断决定一次落库失败会不会删掉用户的原始 Word 与已归档照片，是全项目唯一
// 不可逆的清理路径。它原本是路由 lambda 里的一个 if，没有任何测试。
TEST(WordImportRoutesTest, OnlyARevisionRaceIsRetainedForRetry) {
    using bridge_report::http::PersistFailureDisposition;
    using bridge_report::http::disposition_for_persist_failure;

    // 唯一可恢复的一类：解析已成功、照片已归档，只是没抢到年度台账版本。
    EXPECT_EQ(disposition_for_persist_failure("component_inventory_revision_changed"),
              PersistFailureDisposition::retain_for_retry);

    // 其余全部按原有流程清理。逐个列出来而不是只测一两个：漏判成 retain_for_retry
    // 会把该删的垃圾留在库里，漏判成 discard 会删掉用户重新上传才能拿回的文件。
    for (const auto* code : {
             "import_record_deleted",
             "import_record_wrong_status",
             "db_write_failed",
             "db_commit_failed",
             "contract_validation_failed",
             "",
         }) {
        EXPECT_EQ(disposition_for_persist_failure(code), PersistFailureDisposition::discard)
            << "错误码 " << code << " 不该被当成可重试";
    }
}
