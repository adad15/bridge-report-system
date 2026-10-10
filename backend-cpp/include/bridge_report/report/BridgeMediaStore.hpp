#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "bridge_report/archive/BridgeMediaArchive.hpp"
#include "bridge_report/report/BridgeMediaModels.hpp"

namespace bridge_report::report {

struct StoredBridgeMedia {
    BridgeMediaWriteStatus status{BridgeMediaWriteStatus::Ok};
    std::optional<BridgeMedia> saved;
};

/**
 * @brief 把一张已经落盘的图件记进数据库，并清掉被它顶替的旧文件。
 *
 * 人工上传和生成报告时自动生成地理位置图都走这里，两条路的顺序纪律完全一样：
 *
 * * 提交前失败，只删除本次上传独占的新文件；
 * * 确认提交成功后，才清理被替换的旧文件；
 * * 提交结果不确定时保留新旧文件并记录诊断，核实数据库引用后再清理。
 */
StoredBridgeMedia store_bridge_media(
    const drogon::orm::DbClientPtr& db_client,
    const std::filesystem::path& archive_root,
    const std::string& bridge_id,
    const std::string& slot,
    const archive::ArchivedBridgeMediaFile& archived,
    const std::string& source,
    const std::optional<AutomaticMapCondition>& condition = std::nullopt);

}  // namespace bridge_report::report
