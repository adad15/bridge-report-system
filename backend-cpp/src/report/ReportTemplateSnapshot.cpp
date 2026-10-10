#include "bridge_report/report/ReportTemplateSnapshot.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <json/json.h>
#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/auth/PasswordHash.hpp"
#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::report {
namespace {
Json::Value parse_object(const std::string& text) {
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string error;
    std::istringstream input(text);
    if (!Json::parseFromStream(builder,input,&value,&error) || !value.isObject())
        throw std::runtime_error("模板元数据不是有效对象。");
    return value;
}
}
void prepare_report_template_snapshot(const drogon::orm::DbClientPtr& client,
    const config::AppConfig& config, const std::string& job_id) {
    namespace fs=std::filesystem;
    const auto root=fs::weakly_canonical(config.temporary_report_root);
    const auto directory=archive::resolve_path_under_root(root,job_id);
    const auto expected=(directory/"report.docx").generic_string();
    // Persist ownership before any filesystem write, independently of snapshot commit.
    const auto tracked=client->execSqlSync(
        "update report_job_artifacts set expected_output_path=coalesce(expected_output_path,$2) "
        "where job_id=$1::uuid and cleanup_state='active' returning expected_output_path",job_id,expected);
    if (tracked.empty() || tracked[0]["expected_output_path"].as<std::string>()!=expected)
        throw std::runtime_error("任务目录归属与清理台账不一致。");
    auto latch=std::make_shared<db::CommitLatch>();
    auto tx=client->newTransaction(latch->callback());
    try {
        const auto jobs=tx->execSqlSync(
            "select inspection_year_id::text as year_id,status,template_snapshot_json "
            "from report_generation_jobs where id=$1::uuid for update",job_id);
        if (jobs.empty() || jobs[0]["status"].as<std::string>()!="queued")
            throw std::runtime_error("任务不在模板准备阶段。");
        if (!jobs[0]["template_snapshot_json"].isNull()) return;
        const auto rows=tx->execSqlSync(
            "select t.id::text as id,t.template_code,t.template_name,t.file_id::text as file_id,"
            "t.file_checksum,t.contract_config_json::text as config,t.validation_result_json::text as validation,"
            "t.is_enabled,t.validation_status,af.storage_relative_path "
            "from inspection_report_settings s join report_templates t on t.id=s.template_id "
            "join archived_files af on af.id=t.file_id where s.inspection_year_id=$1::uuid "
            "for update of s,t", jobs[0]["year_id"].as<std::string>());
        if (rows.empty() || !rows[0]["is_enabled"].as<bool>() ||
            rows[0]["validation_status"].as<std::string>()!="valid")
            throw std::runtime_error("所选模板不存在、已停用或未通过校验。");
        const auto& row=rows[0];
        const auto source=archive::resolve_path_under_root(
            config.archive_root,row["storage_relative_path"].as<std::string>());
        if (!fs::is_regular_file(source) || fs::file_size(source)>config.template_upload_max_bytes)
            throw std::runtime_error("模板文件不存在或超过大小限制。");
        std::ifstream input(source,std::ios::binary);
        if (!input) throw std::runtime_error("模板文件无法读取。");
        std::string bytes;
        bytes.resize(static_cast<size_t>(fs::file_size(source)));
        input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        if (input.gcount()!=static_cast<std::streamsize>(bytes.size()) ||
            input.peek()!=std::char_traits<char>::eof())
            throw std::runtime_error("读取模板时文件发生变化。");
        const auto checksum="sha256:"+auth::sha256_hex(bytes);
        if (checksum!=row["file_checksum"].as<std::string>())
            throw std::runtime_error("模板文件摘要与数据库不一致，请重新上传模板。");
        fs::create_directories(directory);
        const auto staging=directory/"template.part";
        {
            std::ofstream out(staging,std::ios::binary|std::ios::trunc);
            out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
            out.close();
            if (!out) throw std::runtime_error("任务模板副本写入失败。");
        }
        fs::rename(staging,directory/"template.docx");
        Json::Value snapshot;
        snapshot["schema_version"]=1;
        snapshot["id"]=row["id"].as<std::string>();
        snapshot["code"]=row["template_code"].as<std::string>();
        snapshot["name"]=row["template_name"].as<std::string>();
        snapshot["file_id"]=row["file_id"].as<std::string>();
        snapshot["checksum"]=checksum;
        snapshot["config"]=parse_object(row["config"].as<std::string>());
        snapshot["validation"]=parse_object(row["validation"].as<std::string>());
        Json::StreamWriterBuilder writer; writer["indentation"]="";
        tx->execSqlSync(
            "update report_generation_jobs set template_id=$2::uuid,template_checksum=$3,"
            "template_snapshot_json=$4::jsonb where id=$1::uuid",
            job_id,snapshot["id"].asString(),checksum,Json::writeString(writer,snapshot));
        tx.reset();
        if (!latch->wait()) throw std::runtime_error("模板快照提交结果未确认，任务没有入队。");
    } catch (...) {
        if (tx) { try {tx->rollback();} catch (...) {} }
        throw;
    }
}
}
