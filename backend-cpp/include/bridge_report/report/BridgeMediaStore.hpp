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
 * * 数据库没写成，刚落盘的文件就是垃圾，当场删掉，再把异常抛出去；
 * * 被顶替的旧文件等数据库改完才删——反过来的话事务一回滚文件就没了；
 * * 新旧路径相同就不删：文件名按内容哈希取，同一张图存两次新旧就是同一个文件，
 *   这时删「旧的」就是把新记录指向的唯一一份删掉。
 */
StoredBridgeMedia store_bridge_media(
    const drogon::orm::DbClientPtr& db_client,
    const std::filesystem::path& archive_root,
    const std::string& bridge_id,
    const std::string& slot,
    const archive::ArchivedBridgeMediaFile& archived,
    const std::string& source);

}  // namespace bridge_report::report
