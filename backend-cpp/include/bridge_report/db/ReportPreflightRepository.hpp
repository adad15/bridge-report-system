#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "bridge_report/report/ReportPreflightModels.hpp"

namespace bridge_report::db {

/**
 * @brief 报告生成前检查（设计 §16、§10.1、§13）。
 *
 * 把"点了生成之后才发现不行"提前到生成页上。每一条都对应 §23.2 的一个稳定错误码，
 * 措辞要让用户知道该去改什么，而不是只说"不满足条件"。
 *
 * 这里只做数据库能判定的部分。两件事留给调用方：
 *
 *  - 归档照片文件在磁盘上是否真的可读（§16 第 8 条的后半段）；
 *  - 本机有没有可用的 Word/WPS 字段更新器（§16 第 9 条）。
 *
 * 两者都不是数据库事实，塞进 SQL 层会让这个类既连数据库又碰文件系统和 COM。
 */
class ReportPreflightRepository {
public:
    explicit ReportPreflightRepository(drogon::orm::DbClientPtr client);

    /// 年度不存在时返回空；其余情况一律返回结论（可能带若干阻断项）。
    std::optional<report::ReportPreflightResult> evaluate(
        const std::string& inspection_year_id) const;

private:
    drogon::orm::DbClientPtr client_;
};

}  // namespace bridge_report::db
