#pragma once

#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/rating_tree/RatingTreeModels.hpp"

namespace bridge_report::db {

enum class RatingTreeSyncStatus {
    Inserted,
    Unchanged,
    ChecksumConflict,
    // 引用的规范包在库里根本没有（或校验和对不上）——真的有问题。
    SourcePackageNotFound,
    // 引用的规范包在库里，但已被停用或同步状态非"正常"。这是**预期状态**：
    // 只保留当前规范版本可用是正常运维，旧评定树因此无法再同步，但它们早已
    // 发布在库里、历史年度照常可用。与 SourcePackageNotFound 分开，是为了让
    // 启动日志里那几行常态提示不至于把真正的失败淹掉。
    SourcePackageDisabled,
    Failed,
};

struct RatingTreeSyncOutcome {
    RatingTreeSyncStatus status{RatingTreeSyncStatus::Failed};
    std::optional<std::string> rating_tree_version_id;
    // SourcePackageDisabled 时填：挡住本次同步的那个规范包（"标准号 版本"），
    // 让提示能点名，而不是让人自己去猜是哪一个。
    std::string blocking_source_package;
};

struct RatingTreeVersionRecord {
    std::string id;
    std::string tree_code;
    std::string tree_name;
    std::string package_version;
    std::string tree_content_checksum;
    std::string technical_condition_package_id;
    std::string maintenance_package_id;
    std::string status;
    std::optional<std::string> published_at;
};

struct RatingTreeProfileBackfillOutcome {
    std::size_t updated_count{0};
    std::size_t ambiguous_profile_count{0};
};

class RatingTreeRepository {
public:
    explicit RatingTreeRepository(drogon::orm::DbClientPtr db_client);

    RatingTreeSyncOutcome sync_published_tree(
        const rating_tree::EffectiveRatingTree& tree,
        int contract_version = 1);

    std::optional<RatingTreeVersionRecord> find_version_by_id(
        const std::string& version_id) const;
    std::optional<RatingTreeVersionRecord> find_published_version(
        const std::string& tree_code,
        const std::string& package_version) const;
    std::vector<RatingTreeVersionRecord> list_published_versions() const;
    std::optional<rating_tree::EffectiveRatingTree> load_published_tree(
        const std::string& version_id) const;

    RatingTreeProfileBackfillOutcome backfill_unique_profile_versions();

private:
    drogon::orm::DbClientPtr db_client_;
};

}  // namespace bridge_report::db
