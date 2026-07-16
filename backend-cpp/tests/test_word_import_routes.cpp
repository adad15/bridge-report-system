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
