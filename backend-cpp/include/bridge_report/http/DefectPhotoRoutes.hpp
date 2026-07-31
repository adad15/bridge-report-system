#pragma once

#include <string>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

#include "bridge_report/archive/ExtractedPhotoArchive.hpp"
#include "bridge_report/config/AppConfig.hpp"
#include "bridge_report/review/ReviewModels.hpp"

namespace bridge_report::http {

/// 一张人工上传照片在本次导入内的编号，两者都在导入范围内递增。
struct UploadedPhotoNaming {
    std::string candidate_id;
    std::string photo_number;
};

/**
 * @brief 在草稿内分配下一个人工照片编号。
 *
 * `photo_number` 取「补-」前缀的最大序号加一，天然避开 Word 的 `2.1-5` 这类编号；
 * `candidate_id` 取 `manual_photo_%04d`，同样只在本次导入内递增。
 */
UploadedPhotoNaming next_uploaded_photo_naming(const Json::Value& draft);

/// 组装人工上传照片的候选对象（设计 §7.5）。
Json::Value build_uploaded_photo_candidate(
    const UploadedPhotoNaming& naming,
    const std::string& defect_candidate_id,
    const std::string& original_file_name,
    const std::string& caption,
    const std::string& archive_relative_path
);

/// 目标病害是否在本次导入的草稿里。
bool draft_has_defect_candidate(const Json::Value& draft, const std::string& defect_candidate_id);

/// 草稿里是否已经占用了该照片候选 ID 或照片编号。
bool draft_has_photo_naming(const Json::Value& draft, const UploadedPhotoNaming& naming);

/// 从草稿里摘掉一个照片候选；返回被摘掉的对象，没有则返回 null。
Json::Value take_photo_candidate(Json::Value& draft, const std::string& photo_candidate_id);

/// 写库结果：失败时 error_code 供路由层直接映射成 HTTP 状态。
struct UploadedPhotoWriteOutcome {
    bool success{false};
    std::string error_code;
    std::string error_message;
};

/**
 * @brief 事务内登记一张已归档的人工照片：两张文件表 + 草稿追加。
 *
 * 调用前文件必须已经落盘；本函数失败时不删文件，由调用方按"先写文件、再开事务、
 * 库写失败就删文件"的既有模式清理。
 */
UploadedPhotoWriteOutcome insert_uploaded_photo(
    const drogon::orm::DbClientPtr& db_client,
    const review::ImportRecordDetail& detail,
    const archive::ArchivedPhotoFile& file,
    const UploadedPhotoNaming& naming,
    const Json::Value& candidate
);

/// 删除结果：成功时带回归档路径与摘要，供调用方删除归档文件。
struct UploadedPhotoDeleteOutcome {
    bool success{false};
    std::string error_code;
    std::string error_message;
    std::string storage_relative_path;
    std::string sha256;
};

/**
 * @brief 事务内注销一张人工照片：草稿移除 + 两张文件表删除。
 *
 * 只接受 `source_ref.source_type = "manual"` 的候选；Word 抽出的照片返回
 * `photo_not_deletable`。归档文件在事务提交后由调用方删除。
 */
UploadedPhotoDeleteOutcome delete_uploaded_photo(
    const drogon::orm::DbClientPtr& db_client,
    const std::string& import_record_id,
    const std::string& photo_candidate_id
);

void register_defect_photo_routes(
    const drogon::orm::DbClientPtr& db_client,
    const config::AppConfig& config
);

}  // 命名空间 bridge_report::http
