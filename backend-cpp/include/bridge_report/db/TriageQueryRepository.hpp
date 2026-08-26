#pragma once

#include <optional>
#include <string>
#include <vector>

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

    /**
     * @brief 一个批次的完整明细，**不分页**。
     *
     * 分页会让"跨页剔除"与"提交清单"对不上：用户在第 3 页去掉两组，提交时前端得凑齐
     * 全部组才能表达"这批除了这两组"。当前最大批次 163 组 / 489 条，只是 JSON 元数据，
     * 一次给完反而简单。
     *
     * 标度、尺寸、照片是**展示字段**，归组模型里没有（它只管身份）；这里按本批次的观测
     * id 单独取，不把展示需求塞进 `TriageObservationInput`。
     *
     * 批次在当前数据下已不存在时返回 `nullopt`——调用方回 `triage_batch_changed`
     * 让前端刷新摘要，而不是把一个过期批次当空批次交给用户。
     */
    [[nodiscard]] std::optional<Json::Value> batch_detail(
        const std::string& bridge_id, const std::string& batch_id) const;

private:
    [[nodiscard]] std::vector<review::TriageThreadInput> load_threads(
        const std::string& bridge_id) const;

    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
