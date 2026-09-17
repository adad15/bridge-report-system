#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/report/BridgeMediaModels.hpp"

namespace bridge_report::db {

/// 写入一张图件的结果：状态，外加被它顶掉的那张旧图（如果有）。
struct BridgeMediaSaveOutcome {
    report::BridgeMediaWriteStatus status{report::BridgeMediaWriteStatus::Ok};
    report::BridgeMediaReplacement replaced;
    std::optional<report::BridgeMedia> saved;
};

/**
 * @brief 桥梁图件的读写。
 *
 * 文件本身存在 `archived_files` 里，这张表只记「哪座桥的哪个槽位用哪个文件」。
 *
 * **换图是覆盖。** 一个槽位只有一张图，新的顶掉旧的；旧的归档行在同一个事务里删掉，
 * 磁盘上那个文件由调用方在事务提交之后删——事务回滚了文件却没了是最难查的一类错。
 */
class BridgeMediaRepository {
public:
    explicit BridgeMediaRepository(drogon::orm::DbClientPtr client);

    std::vector<report::BridgeMedia> list(const std::string& bridge_id) const;

    /// 按 id 取一张图，用来出图片内容。
    std::optional<report::BridgeMedia> find(const std::string& media_id) const;

    BridgeMediaSaveOutcome save(
        const std::string& bridge_id, const report::BridgeMediaInput& input) const;

    /// 删掉一个槽位的图；没有就返回空。返回值里是要从磁盘删的那个文件。
    std::optional<report::BridgeMediaReplacement> remove(
        const std::string& bridge_id, const std::string& slot) const;

private:
    drogon::orm::DbClientPtr client_;
};

}  // namespace bridge_report::db
