#pragma once

#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/review/ReviewModels.hpp"

namespace bridge_report::db {

/**
 * @brief 桥梁 -> 年度 -> 导入记录导航只读查询。
 *
 * 注意：内部使用 execSqlSync，会阻塞调用方所在线程；
 * 本地单用户 v1 场景可接受（与 /health/db 的取舍一致）。
 */
class ReviewRepository {
public:
    explicit ReviewRepository(drogon::orm::DbClientPtr db_client);

    std::vector<review::BridgeSummary> list_bridges();
    std::vector<review::InspectionYearSummary> list_inspection_years(const std::string& bridge_id);
    std::vector<review::ImportRecordSummary> list_import_records(const std::string& bridge_id);

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // 命名空间 bridge_report::db
