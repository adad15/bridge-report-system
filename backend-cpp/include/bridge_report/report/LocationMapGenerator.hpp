#pragma once

#include <functional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/config/AppConfig.hpp"

namespace bridge_report::report {

/// 生成报告前处理地理位置图的结果。写进任务进度，排障时一眼看出图是怎么来的。
enum class LocationMapStatus {
    /// 按档案坐标重新取了一张。
    Generated,
    /// 管理员自己上传过一张，那张优先，不覆盖。
    KeptManual,
    /// 档案里没有坐标，没有依据就不生成。
    NoCoordinates,
    /// 本机没配地图的 Web 服务 key。
    NoKey,
    /// 取图失败，沿用上一次生成的那张。
    FailedKeptExisting,
    /// 取图失败，之前也没有，§1.1 这次不出地理位置图。
    FailedNone,
};

std::string location_map_status_text(LocationMapStatus status);

struct LocationMapOutcome {
    LocationMapStatus status{LocationMapStatus::NoCoordinates};
    /// 失败时的原因，面向人读。
    std::string detail;
};

/**
 * @brief 取一张静态地图。
 *
 * 给定请求体（key 与 WGS-84 坐标），返回图片字节；取不到就抛 std::runtime_error，
 * what() 面向人读。生产里走 Python 工具服务；测试里换成桩——drogon 的同步 HTTP 客户端
 * 离开运行中的后端进程会一直挂着，不能在测试里真发请求。
 */
using StaticMapFetcher = std::function<std::string(const Json::Value& request)>;

/// 生产用的取图方式：交给 Python 工具服务的 /map/static-image。
StaticMapFetcher python_static_map_fetcher(const config::AppConfig& config);

/**
 * @brief 生成报告前，按档案坐标刷新这座桥的地理位置图。
 *
 * 每次生成报告都重新取一张：坐标改过之后，图也就跟着对了，不用另外维护「图是按哪对坐标
 * 生成的」这件事。
 *
 * **绝不因为地图让报告生成失败。** 取不到就沿用上一次那张；上一次也没有，§1.1 就不出这张
 * 图，引用句也跟着不写。地图服务是外部依赖，它不好使是常态。
 *
 * 管理员手工上传的地理位置图优先：那通常是标注过的截图，比自动取的更合用，不能被覆盖。
 */
LocationMapOutcome refresh_location_map(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config,
    const std::string& inspection_year_id,
    const StaticMapFetcher& fetch);

}  // namespace bridge_report::report
