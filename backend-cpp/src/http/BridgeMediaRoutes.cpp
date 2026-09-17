#include "bridge_report/http/BridgeMediaRoutes.hpp"

#include <filesystem>
#include <optional>
#include <string>

#include "bridge_report/archive/ArchivePaths.hpp"
#include "bridge_report/archive/BridgeMediaArchive.hpp"
#include "bridge_report/db/BridgeMediaRepository.hpp"
#include "bridge_report/http/AuthRoutes.hpp"
#include "bridge_report/http/RouteHelpers.hpp"
#include "bridge_report/report/BridgeMediaStore.hpp"

namespace bridge_report::http {
namespace {

void respond_bridge_not_found(const HttpCallback& callback) {
    respond_json(callback, make_error_body("bridge_not_found", "桥梁不存在。"), drogon::k404NotFound);
}

void respond_unknown_slot(const HttpCallback& callback) {
    respond_json(callback, make_error_body("bridge_media_slot_unknown", "未知的图件位置。"),
                 drogon::k404NotFound);
}

/// 扩展名到响应类型。归档里只存这几种，都是 detect_image_extension 认过的。
std::string content_type_for(const std::string& extension) {
    if (extension == ".png") return "image/png";
    if (extension == ".gif") return "image/gif";
    if (extension == ".bmp") return "image/bmp";
    if (extension == ".webp") return "image/webp";
    if (extension == ".tiff") return "image/tiff";
    return "image/jpeg";
}

std::optional<std::string> find_bridge_system_number(
    const drogon::orm::DbClientPtr& db_client, const std::string& bridge_id) {
    const auto rows = db_client->execSqlSync(
        "select system_number from bridges where id = $1::uuid", bridge_id);
    if (rows.empty()) return std::nullopt;
    return rows[0]["system_number"].as<std::string>();
}

/// 管理员身份检查：没登录 401，登录了但不是管理员 403。
bool require_admin(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback) {
    const auto user = authenticate_request(db_client, request);
    if (!user.has_value()) {
        respond_unauthorized(callback);
        return false;
    }
    if (!user->is_admin()) {
        respond_forbidden(callback);
        return false;
    }
    return true;
}

void handle_list(
    const drogon::orm::DbClientPtr& db_client,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    const std::string& bridge_id) {
    if (!is_valid_uuid(bridge_id)) {
        respond_bridge_not_found(callback);
        return;
    }
    if (!authenticate_request(db_client, request).has_value()) {
        respond_unauthorized(callback);
        return;
    }
    Json::Value body;
    body["media"] = report::bridge_media_list_json(db::BridgeMediaRepository(db_client).list(bridge_id));
    respond_json(callback, body);
}

void handle_upload(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    const std::string& bridge_id,
    const std::string& slot) {
    if (!is_valid_uuid(bridge_id)) {
        respond_bridge_not_found(callback);
        return;
    }
    if (!report::is_bridge_media_slot(slot)) {
        respond_unknown_slot(callback);
        return;
    }
    if (!require_admin(db_client, request, callback)) return;

    drogon::MultiPartParser parser;
    if (parser.parse(request) != 0) {
        respond_json(callback, make_error_body("bridge_media_invalid", "上传内容不是合法的 multipart 表单。"),
                     drogon::k400BadRequest);
        return;
    }
    const auto& files = parser.getFiles();
    if (files.size() != 1 || files[0].getItemName() != "file") {
        respond_json(callback, make_error_body("bridge_media_invalid", "必须上传一个名为 file 的图片。"),
                     drogon::k400BadRequest);
        return;
    }

    const auto system_number = find_bridge_system_number(db_client, bridge_id);
    if (!system_number.has_value()) {
        respond_bridge_not_found(callback);
        return;
    }

    archive::BridgeMediaArchiveContext context;
    context.archive_root = config.archive_root;
    context.bridge_system_number = *system_number;
    context.slot = slot;
    context.max_bytes = config.photo_upload_max_bytes;

    archive::ArchivedBridgeMediaFile archived;
    try {
        archived = archive::archive_bridge_media(files[0].fileContent(), files[0].getFileName(), context);
    } catch (const archive::BridgeMediaTooLargeError&) {
        respond_json(callback, make_error_body("bridge_media_too_large", "图片超过允许的大小上限。"),
                     drogon::k413RequestEntityTooLarge);
        return;
    } catch (const std::exception&) {
        respond_json(callback, make_error_body("bridge_media_invalid", "上传的文件不是受支持的图片。"),
                     drogon::k400BadRequest);
        return;
    }

    // 人工上传的地理位置图优先于生成报告时自动取的那张，来源标记就是区分它们的依据。
    const auto stored = report::store_bridge_media(
        db_client, config.archive_root, bridge_id, slot, archived, "人工上传");
    switch (stored.status) {
        case report::BridgeMediaWriteStatus::BridgeNotFound:
            respond_bridge_not_found(callback);
            return;
        case report::BridgeMediaWriteStatus::UnknownSlot:
            respond_unknown_slot(callback);
            return;
        case report::BridgeMediaWriteStatus::Ok:
            break;
    }
    Json::Value body;
    body["media"] = stored.saved.has_value() ? stored.saved->to_json() : Json::Value(Json::nullValue);
    respond_json(callback, body);
}

void handle_delete(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    const std::string& bridge_id,
    const std::string& slot) {
    if (!is_valid_uuid(bridge_id)) {
        respond_bridge_not_found(callback);
        return;
    }
    if (!report::is_bridge_media_slot(slot)) {
        respond_unknown_slot(callback);
        return;
    }
    if (!require_admin(db_client, request, callback)) return;

    const auto removed = db::BridgeMediaRepository(db_client).remove(bridge_id, slot);
    if (removed.has_value() && removed->storage_relative_path.has_value()) {
        archive::remove_archived_bridge_media(config.archive_root, *removed->storage_relative_path,
                                              removed->file_hash.value_or(""));
    }
    Json::Value body;
    body["deleted"] = removed.has_value();
    respond_json(callback, body);
}

void handle_content(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config,
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    const std::string& media_id) {
    if (!is_valid_uuid(media_id)) {
        respond_json(callback, make_error_body("bridge_media_not_found", "图件不存在。"),
                     drogon::k404NotFound);
        return;
    }
    const auto media = db::BridgeMediaRepository(db_client).find(media_id);
    if (!media.has_value()) {
        respond_json(callback, make_error_body("bridge_media_not_found", "图件不存在。"),
                     drogon::k404NotFound);
        return;
    }
    const std::filesystem::path relative(media->storage_relative_path);
    if (!archive::is_safe_archive_relative_path(relative)) {
        respond_json(callback, make_error_body("unsafe_archive_path", "图件归档路径不安全。"),
                     drogon::k400BadRequest);
        return;
    }
    const auto absolute = archive::resolve_path_under_root(config.archive_root, relative);
    if (!std::filesystem::is_regular_file(absolute)) {
        respond_json(callback, make_error_body("bridge_media_file_missing", "图件归档文件不存在。"),
                     drogon::k409Conflict);
        return;
    }
    auto response = drogon::HttpResponse::newFileResponse(
        absolute.string(), "", drogon::CT_CUSTOM, content_type_for(media->file_extension), request);
    apply_local_dev_cors_headers(response);
    callback(response);
}

}  // namespace

void register_bridge_media_routes(
    const drogon::orm::DbClientPtr& db_client, const config::AppConfig& config) {
    const std::string collection = "/api/bridges/{bridge_id}/media";
    const std::string slot_path = "/api/bridges/{bridge_id}/media/{slot}";
    // 内容那条不鉴权，和既有的病害照片内容路由一致：<img src> 带不上 Authorization 头。
    const std::string content_path = "/api/bridge-media/{media_id}/content";
    register_options_handler(collection);
    register_options_handler(slot_path);
    register_options_handler(content_path);

    drogon::app().registerHandler(
        collection,
        [db_client](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                    const std::string& bridge_id) {
            try {
                handle_list(db_client, request, callback, bridge_id);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        slot_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& bridge_id, const std::string& slot) {
            try {
                handle_upload(db_client, config, request, callback, bridge_id, slot);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        slot_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& bridge_id, const std::string& slot) {
            try {
                handle_delete(db_client, config, request, callback, bridge_id, slot);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        content_path,
        [db_client, config](const drogon::HttpRequestPtr& request, HttpCallback&& callback,
                            const std::string& media_id) {
            try {
                handle_content(db_client, config, request, callback, media_id);
            } catch (const std::filesystem::filesystem_error&) {
                respond_json(callback, make_error_body("bridge_media_file_missing", "图件归档文件无法读取。"),
                             drogon::k409Conflict);
            } catch (...) {
                respond_db_unavailable(callback);
            }
        },
        {drogon::Get});
}

}  // namespace bridge_report::http
