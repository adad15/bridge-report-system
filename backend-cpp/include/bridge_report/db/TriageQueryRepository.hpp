#pragma once

#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/review/ThreadTriageGrouping.hpp"

namespace bridge_report::db {

/**
 * @brief 线索整理工作台的只读取数：把当前有效事实喂给 `build_triage_model`。
 *
 * **只读**——不写库，也不把批次物化。批次是展示与确认单位，构件改名、观测重绑、年度修订
 * 都会让它变；存成表就得配一整套失效重算，而 1197 条的实时归组不过是一次 group by。
 *
 * 取数一次取完，不按构件循环：现有整理页每张卡各发一次候选请求，1197 张卡把浏览器
 * 连接池堵死——同样的错误在服务端就是 N+1 查询。
 */
class TriageQueryRepository {
public:
    explicit TriageQueryRepository(drogon::orm::DbClientPtr db_client);

    /// 归组模型：未绑定的正式观测 + 相关构件上的已有线索。
    [[nodiscard]] review::TriageModel load_model(const std::string& bridge_id) const;

    /// 工作台摘要 JSON（见 `review::triage_summary_json`）。
    [[nodiscard]] Json::Value summary(const std::string& bridge_id) const;

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
