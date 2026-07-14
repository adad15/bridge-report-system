#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/db/ReviewRepository.hpp"

namespace bridge_report::db {

/**
 * @brief 模块 06 只读构件病害档案查询。
 *
 * 统一事实口径：默认只读各年度当前有效版本（inspection_years.is_current AND
 * status='已确认'）中 review_status IN ('已确认','已修改') 的正式观测；
 * 旧修订版仅经 revisions 入口独立返回，不进入默认统计。
 * 本仓库不含任何写操作；线索创建与绑定见 DefectThreadRepository。
 */
class ComponentArchiveRepository {
public:
    explicit ComponentArchiveRepository(drogon::orm::DbClientPtr db_client);

    /// 曾在当前有效版本中出现正式病害的构件列表（含线索数/未绑定数/首末年份/最新构件评分）。
    Json::Value list_components(const std::string& bridge_id);

    /// 构件基本信息；不存在时返回空。
    std::optional<Json::Value> get_component(const std::string& component_id);

    /// 构件档案：按线索组织的历年观测 + 未绑定观测 + 各年度构件评分。
    Json::Value get_defect_archive(const std::string& component_id);

    /// 旧修订版观测，按 年份+版本号 分组只读返回，并标注当前有效版本。
    Json::Value get_revisions(const std::string& component_id, const std::string& bridge_id);

    /// 全桥未绑定线索的当前有效正式观测（线索整理页数据源）。
    Json::Value list_unbound_observations(const std::string& bridge_id);

    /// 观测来源证据：原始行、表名/表序/行号、导入记录与归档文件编号。
    std::optional<Json::Value> get_observation_evidence(const std::string& observation_id);

    /// 线索建议输入：观测的构件、类型与详细位置；不存在时返回空。
    std::optional<Json::Value> get_observation_summary(const std::string& observation_id);

    /// 同一构件的全部线索（含首见/末见年份），供档案分组与建议候选共用。
    Json::Value list_threads_for_component(const std::string& component_id);

    /// 正式病害照片的归档文件定位（经 defect_photos.archived_file_id）。
    std::optional<PhotoContentRef> get_defect_photo_content_ref(const std::string& defect_photo_id);

private:
    drogon::orm::DbClientPtr db_client_;
};

/**
 * @brief 纯函数：把仓库取出的扁平行数组组装成"线索一级、年度二级"的档案响应。
 *
 * threads / observations 均为扁平 JSON 数组；observations 已携带 measurements/photos
 * 子数组与 defect_thread_id。分组规则：观测按 defect_thread_id 归入对应线索的
 * observations[]（保持输入的年度倒序），无线索的进入 unbound_observations[]；
 * 没有任何当前观测的线索仍保留（空 observations[]）。前端不再重组。
 */
[[nodiscard]] Json::Value assemble_defect_archive(
    const Json::Value& component,
    const Json::Value& ratings,
    const Json::Value& threads,
    const Json::Value& observations
);

}  // namespace bridge_report::db
