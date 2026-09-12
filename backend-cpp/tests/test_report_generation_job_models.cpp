#include <string>

#include <gtest/gtest.h>

#include "bridge_report/report/ReportGenerationJobModels.hpp"

namespace {

using bridge_report::report::JobStatus;
using bridge_report::report::ReportFilenameParts;
using bridge_report::report::ReportGenerationJob;
using bridge_report::report::build_report_filename;
using bridge_report::report::job_is_final;
using bridge_report::report::job_is_running;
using bridge_report::report::job_status_from_text;
using bridge_report::report::job_status_text;

// 状态名是接口契约：前端按它显示阶段，数据库 check 约束也照它写。
TEST(ReportGenerationJobStatus, EveryStatusRoundTripsThroughItsDatabaseText) {
    for (const auto status : {JobStatus::Queued, JobStatus::ValidatingData,
                              JobStatus::AssemblingDocx, JobStatus::UpdatingFields,
                              JobStatus::ValidatingDocx, JobStatus::Ready,
                              JobStatus::Failed, JobStatus::Expired}) {
        const auto text = job_status_text(status);
        const auto parsed = job_status_from_text(text);
        ASSERT_TRUE(parsed.has_value()) << text;
        EXPECT_EQ(*parsed, status) << text;
    }
}

TEST(ReportGenerationJobStatus, UnknownTextIsRejectedRatherThanGuessed) {
    EXPECT_FALSE(job_status_from_text("done").has_value());
    EXPECT_FALSE(job_status_from_text("").has_value());
}

// 进行中的状态会阻断年度和桥梁删除（设计 §17.4），分类错了就会放行一次删除，
// 把正在生成的任务连同年度一起删掉。
TEST(ReportGenerationJobStatus, RunningAndFinalPartitionEveryStatus) {
    EXPECT_TRUE(job_is_running(JobStatus::Queued));
    EXPECT_TRUE(job_is_running(JobStatus::ValidatingData));
    EXPECT_TRUE(job_is_running(JobStatus::AssemblingDocx));
    EXPECT_TRUE(job_is_running(JobStatus::UpdatingFields));
    EXPECT_TRUE(job_is_running(JobStatus::ValidatingDocx));

    for (const auto status : {JobStatus::Ready, JobStatus::Failed, JobStatus::Expired}) {
        EXPECT_FALSE(job_is_running(status));
        EXPECT_TRUE(job_is_final(status));
    }
}

TEST(ReportGenerationJob, OnlyReadyWithAFileCanBeDownloaded) {
    ReportGenerationJob job;
    job.status = JobStatus::Ready;
    EXPECT_FALSE(job.can_download()) << "ready 但没有文件路径时不能给下载";

    job.temporary_file_path = "runtime/temp/report-jobs/j/report.docx";
    EXPECT_TRUE(job.can_download());

    job.status = JobStatus::Failed;
    EXPECT_FALSE(job.can_download()) << "失败任务不能提供下载（设计 §17.2）";
}

// 临时路径是服务器上的绝对路径，对前端没用；出接口只会多一条攻击面。
TEST(ReportGenerationJob, JsonNeverExposesTheServerSidePath) {
    ReportGenerationJob job;
    job.id = "job-1";
    job.status = JobStatus::Ready;
    job.temporary_file_path = "D:/runtime/temp/report-jobs/job-1/report.docx";
    job.download_filename = "百股大桥定期检测报告（2类）.docx";

    const auto json = job.to_json();

    EXPECT_FALSE(json.isMember("temporary_file_path"));
    EXPECT_TRUE(json["can_download"].asBool());
    EXPECT_EQ(json["download_filename"].asString(), *job.download_filename);
    EXPECT_EQ(json["status"].asString(), "ready");
}

// 设计 §20 的文件名规则。
TEST(ReportFilename, JoinsEveryPartInTheSpecifiedOrder) {
    ReportFilenameParts parts;
    parts.report_number = "Q202604001-JZ-113";
    parts.administrative_region = "太和区";
    parts.route_code = "S320";
    parts.route_name = "大养线";
    parts.bridge_name = "百股大桥";
    parts.overall_grade = "2类";

    EXPECT_EQ(build_report_filename(parts),
              "Q202604001-JZ-113太和区S320大养线百股大桥定期检测报告（2类）.docx");
}

// 缺失的片段直接省略，不留 null、连续占位符或多余空格（设计 §20）。
TEST(ReportFilename, OmitsMissingPartsWithoutLeavingGaps) {
    ReportFilenameParts parts;
    parts.bridge_name = "百股大桥";

    EXPECT_EQ(build_report_filename(parts), "百股大桥定期检测报告.docx");
}

TEST(ReportFilename, BlankPartsCountAsMissing) {
    ReportFilenameParts parts;
    parts.report_number = "   ";
    parts.route_name = "";
    parts.bridge_name = "百股大桥";
    parts.overall_grade = " ";

    EXPECT_EQ(build_report_filename(parts), "百股大桥定期检测报告.docx");
}

// 只在文件名这一层清理非法字符，不改文档内部的业务值。路线名里真有个斜杠时，
// 报告正文仍要原样印出来。
TEST(ReportFilename, StripsCharactersWindowsCannotPutInAFileName) {
    ReportFilenameParts parts;
    parts.route_name = "大养线/东段";
    parts.bridge_name = "百股\"大桥\"";
    parts.overall_grade = "2类";

    const auto name = build_report_filename(parts);

    EXPECT_EQ(name, "大养线东段百股大桥定期检测报告（2类）.docx");
    for (const char illegal : std::string("<>:\"/\\|?*")) {
        EXPECT_EQ(name.find(illegal), std::string::npos) << illegal;
    }
}

// 只剩非法字符的片段等于没有这一项，不能在名字里留一个空壳。
TEST(ReportFilename, APartMadeOnlyOfIllegalCharactersDisappears) {
    ReportFilenameParts parts;
    parts.report_number = "///";
    parts.bridge_name = "百股大桥";

    EXPECT_EQ(build_report_filename(parts), "百股大桥定期检测报告.docx");
}

}  // namespace
