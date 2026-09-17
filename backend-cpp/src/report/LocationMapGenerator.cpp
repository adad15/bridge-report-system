#include "bridge_report/report/LocationMapGenerator.hpp"

#include <stdexcept>

#include <drogon/HttpClient.h>
#include <drogon/utils/Utilities.h>
#include <trantor/utils/Logger.h>

#include "bridge_report/archive/BridgeMediaArchive.hpp"
#include "bridge_report/archive/ImageContent.hpp"
#include "bridge_report/report/BridgeMediaStore.hpp"

namespace bridge_report::report {
namespace {

constexpr const char* kLocationMapSlot = "LOCATION_MAP";
/// 自动生成的图在归档里的来源标记，和数据库 check 约束里的写法一致。
constexpr const char* kGeneratedSource = "按坐标生成";
constexpr const char* kManualSource = "人工上传";

struct BridgeLocation {
    std::string bridge_id;
    std::string system_number;
    std::string bridge_name;
    std::optional<double> longitude;
    std::optional<double> latitude;
    /// 当前地理位置图的来源；没有图时为空。
    std::optional<std::string> existing_source;
};

std::optional<BridgeLocation> find_location(
    const drogon::orm::DbClientPtr& db_client, const std::string& inspection_year_id) {
    const auto rows = db_client->execSqlSync(
        "select b.id::text as bridge_id, b.system_number, b.bridge_name, b.longitude, b.latitude, "
        "  (select m.source from bridge_media m where m.bridge_id = b.id and m.slot = $2) as source "
        "from inspection_years iy join bridges b on b.id = iy.bridge_id "
        "where iy.id = $1::uuid",
        inspection_year_id, std::string(kLocationMapSlot));
    if (rows.empty()) return std::nullopt;
    const auto& row = rows[0];
    BridgeLocation location;
    location.bridge_id = row["bridge_id"].as<std::string>();
    location.system_number = row["system_number"].as<std::string>();
    location.bridge_name = row["bridge_name"].as<std::string>();
    if (!row["longitude"].isNull()) location.longitude = row["longitude"].as<double>();
    if (!row["latitude"].isNull()) location.latitude = row["latitude"].as<double>();
    if (!row["source"].isNull()) location.existing_source = row["source"].as<std::string>();
    return location;
}

/// 取图失败时的结论：有旧图就沿用，没有就这次不出。
LocationMapOutcome failed(const BridgeLocation& location, std::string detail) {
    return {location.existing_source.has_value() ? LocationMapStatus::FailedKeptExisting
                                                 : LocationMapStatus::FailedNone,
            std::move(detail)};
}

}  // namespace

StaticMapFetcher python_static_map_fetcher(const config::AppConfig& config) {
    const auto base_url = config.python_tools_base_url;
    return [base_url](const Json::Value& ask) -> std::string {
        // 本机这套 drogon 的 HTTPS 客户端连不出去，取图交给 Python 工具服务；
        // 坐标原样传 WGS-84，换算成 GCJ-02 在那边做。
        auto client = drogon::HttpClient::newHttpClient(base_url);
        auto request = drogon::HttpRequest::newHttpJsonRequest(ask);
        request->setMethod(drogon::Post);
        request->setPath("/map/static-image");
        const auto [result, response] = client->sendRequest(request, 30.0);
        if (result != drogon::ReqResult::Ok || !response) {
            throw std::runtime_error("Python 工具服务没有响应。");
        }
        const auto payload = response->getJsonObject();
        if (!payload) throw std::runtime_error("地图服务返回了无法解析的内容。");
        if (response->statusCode() != drogon::k200OK) {
            const auto& detail = (*payload)["detail"];
            throw std::runtime_error(detail["message"].isString() ? detail["message"].asString()
                                                                  : std::string("地图服务返回了失败。"));
        }
        return drogon::utils::base64Decode((*payload)["image_base64"].asString());
    };
}

std::string location_map_status_text(LocationMapStatus status) {
    switch (status) {
        case LocationMapStatus::Generated: return "generated";
        case LocationMapStatus::KeptManual: return "kept_manual";
        case LocationMapStatus::NoCoordinates: return "no_coordinates";
        case LocationMapStatus::NoKey: return "no_key";
        case LocationMapStatus::FailedKeptExisting: return "failed_kept_existing";
        case LocationMapStatus::FailedNone: return "failed_none";
    }
    return "unknown";
}

LocationMapOutcome refresh_location_map(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config,
    const std::string& inspection_year_id,
    const StaticMapFetcher& fetch) {
    const auto location = find_location(db_client, inspection_year_id);
    if (!location.has_value()) return {LocationMapStatus::NoCoordinates, "年度检查不存在。"};

    if (location->existing_source == std::optional<std::string>(kManualSource)) {
        return {LocationMapStatus::KeptManual, ""};
    }
    if (!location->longitude.has_value() || !location->latitude.has_value()) {
        return {LocationMapStatus::NoCoordinates, ""};
    }
    if (config.map.web_service_key.empty()) {
        return {LocationMapStatus::NoKey, ""};
    }

    Json::Value ask;
    ask["key"] = config.map.web_service_key;
    ask["longitude"] = *location->longitude;
    ask["latitude"] = *location->latitude;

    std::string image;
    try {
        image = fetch(ask);
    } catch (const std::exception& error) {
        return failed(*location, error.what());
    }
    if (archive::detect_image_extension(image).empty()) {
        return failed(*location, "地图服务返回的不是图片。");
    }

    archive::BridgeMediaArchiveContext context;
    context.archive_root = config.archive_root;
    context.bridge_system_number = location->system_number;
    context.slot = kLocationMapSlot;
    context.max_bytes = config.photo_upload_max_bytes;
    try {
        const auto archived = archive::archive_bridge_media(
            image, location->bridge_name + "-地理位置图", context);
        const auto stored = store_bridge_media(db_client, config.archive_root, location->bridge_id,
                                               kLocationMapSlot, archived, kGeneratedSource);
        if (stored.status != BridgeMediaWriteStatus::Ok) {
            return failed(*location, "地理位置图没能存进档案。");
        }
    } catch (const std::exception& error) {
        LOG_WARN << "location map archive failed bridge=" << location->bridge_id
                 << " detail=" << error.what();
        return failed(*location, "地理位置图没能存进档案。");
    }
    return {LocationMapStatus::Generated, ""};
}

}  // namespace bridge_report::report
