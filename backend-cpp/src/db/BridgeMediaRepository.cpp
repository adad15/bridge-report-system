#include "bridge_report/db/BridgeMediaRepository.hpp"

#include <utility>

#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {
namespace {

constexpr const char* kMediaColumns =
    "m.id::text as id, m.bridge_id::text as bridge_id, m.slot, "
    "m.archived_file_id::text as archived_file_id, m.source, "
    "to_char(m.created_at, 'YYYY-MM-DD HH24:MI:SS') as created_at, "
    "to_char(m.updated_at, 'YYYY-MM-DD HH24:MI:SS') as updated_at, "
    "f.original_file_name, f.storage_relative_path, "
    "coalesce(f.file_extension, '') as file_extension, "
    "coalesce(f.file_size_bytes, 0) as file_size_bytes";

report::BridgeMedia read_media(const drogon::orm::Row& row) {
    report::BridgeMedia media;
    media.id = row["id"].as<std::string>();
    media.bridge_id = row["bridge_id"].as<std::string>();
    media.slot = row["slot"].as<std::string>();
    media.archived_file_id = row["archived_file_id"].as<std::string>();
    media.original_file_name = row["original_file_name"].as<std::string>();
    media.storage_relative_path = row["storage_relative_path"].as<std::string>();
    media.file_extension = row["file_extension"].as<std::string>();
    media.file_size_bytes = static_cast<std::uintmax_t>(row["file_size_bytes"].as<int64_t>());
    media.source = row["source"].as<std::string>();
    media.created_at = row["created_at"].as<std::string>();
    media.updated_at = row["updated_at"].as<std::string>();
    return media;
}

}  // namespace

BridgeMediaRepository::BridgeMediaRepository(drogon::orm::DbClientPtr client)
    : client_(std::move(client)) {}

std::vector<report::BridgeMedia> BridgeMediaRepository::list(const std::string& bridge_id) const {
    const auto rows = client_->execSqlSync(
        std::string("select ") + kMediaColumns +
            " from bridge_media m join archived_files f on f.id = m.archived_file_id"
            " where m.bridge_id = $1::uuid order by m.slot",
        bridge_id);
    std::vector<report::BridgeMedia> items;
    items.reserve(rows.size());
    for (const auto& row : rows) items.push_back(read_media(row));
    return items;
}

std::optional<report::BridgeMedia> BridgeMediaRepository::find(const std::string& media_id) const {
    const auto rows = client_->execSqlSync(
        std::string("select ") + kMediaColumns +
            " from bridge_media m join archived_files f on f.id = m.archived_file_id"
            " where m.id = $1::uuid",
        media_id);
    if (rows.empty()) return std::nullopt;
    return read_media(rows[0]);
}

BridgeMediaSaveOutcome BridgeMediaRepository::save(
    const std::string& bridge_id, const report::BridgeMediaInput& input) const {
    if (!report::is_bridge_media_slot(input.slot)) {
        return {report::BridgeMediaWriteStatus::UnknownSlot, {}, std::nullopt};
    }

    std::shared_ptr<drogon::orm::Transaction> tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = client_->newTransaction(latch->callback());

        const auto bridge = tx->execSqlSync(
            "select system_number, bridge_name from bridges where id = $1::uuid", bridge_id);
        if (bridge.empty()) {
            tx->rollback();
            return {report::BridgeMediaWriteStatus::BridgeNotFound, {}, std::nullopt};
        }

        // 这个槽位原来那张图：先记下来，事务提交之后调用方才去删文件。
        report::BridgeMediaReplacement replaced;
        const auto previous = tx->execSqlSync(
            "select m.archived_file_id::text as archived_file_id, f.storage_relative_path, "
            "coalesce(f.file_hash, '') as file_hash from bridge_media m "
            "join archived_files f on f.id = m.archived_file_id "
            "where m.bridge_id = $1::uuid and m.slot = $2",
            bridge_id, input.slot);
        if (!previous.empty()) {
            replaced.archived_file_id = previous[0]["archived_file_id"].as<std::string>();
            replaced.storage_relative_path = previous[0]["storage_relative_path"].as<std::string>();
            replaced.file_hash = previous[0]["file_hash"].as<std::string>();
        }

        const auto file = tx->execSqlSync(
            "insert into archived_files(bridge_id, original_file_name, current_file_name, "
            "storage_relative_path, file_type, file_purpose, file_extension, file_size_bytes, "
            "file_hash, source_description) "
            "values($1::uuid, $2, $3, $4, '图片', $5, $6, $7, nullif($8, ''), $9) "
            "returning id::text as id",
            bridge_id, input.original_file_name, input.original_file_name,
            input.storage_relative_path,
            std::string("桥梁图件·") + std::string(report::bridge_media_slot_label(input.slot)),
            input.file_extension, static_cast<int64_t>(input.file_size_bytes), input.file_hash,
            input.source);
        const auto archived_file_id = file[0]["id"].as<std::string>();

        tx->execSqlSync(
            "insert into bridge_media(bridge_id, slot, archived_file_id, source) "
            "values($1::uuid, $2, $3::uuid, $4) "
            "on conflict (bridge_id, slot) do update set "
            " archived_file_id = excluded.archived_file_id, "
            " source = excluded.source, "
            " updated_at = now()",
            bridge_id, input.slot, archived_file_id, input.source);

        if (replaced.archived_file_id.has_value()) {
            tx->execSqlSync("delete from archived_files where id = $1::uuid",
                            *replaced.archived_file_id);
        }

        const auto saved = tx->execSqlSync(
            std::string("select ") + kMediaColumns +
                " from bridge_media m join archived_files f on f.id = m.archived_file_id"
                " where m.bridge_id = $1::uuid and m.slot = $2",
            bridge_id, input.slot);
        return {report::BridgeMediaWriteStatus::Ok, replaced,
                saved.empty() ? std::nullopt : std::optional(read_media(saved[0]))};
    } catch (const drogon::orm::DrogonDbException&) {
        if (tx != nullptr) tx->rollback();
        throw;
    }
}

std::optional<report::BridgeMediaReplacement> BridgeMediaRepository::remove(
    const std::string& bridge_id, const std::string& slot) const {
    std::shared_ptr<drogon::orm::Transaction> tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = client_->newTransaction(latch->callback());
        const auto rows = tx->execSqlSync(
            "select m.archived_file_id::text as archived_file_id, f.storage_relative_path, "
            "coalesce(f.file_hash, '') as file_hash from bridge_media m "
            "join archived_files f on f.id = m.archived_file_id "
            "where m.bridge_id = $1::uuid and m.slot = $2",
            bridge_id, slot);
        if (rows.empty()) {
            tx->rollback();
            return std::nullopt;
        }
        report::BridgeMediaReplacement removed;
        removed.archived_file_id = rows[0]["archived_file_id"].as<std::string>();
        removed.storage_relative_path = rows[0]["storage_relative_path"].as<std::string>();
        removed.file_hash = rows[0]["file_hash"].as<std::string>();

        // 先删引用再删归档行：外键是 restrict，反过来会被数据库挡住。
        tx->execSqlSync("delete from bridge_media where bridge_id = $1::uuid and slot = $2",
                        bridge_id, slot);
        tx->execSqlSync("delete from archived_files where id = $1::uuid", *removed.archived_file_id);
        return removed;
    } catch (const drogon::orm::DrogonDbException&) {
        if (tx != nullptr) tx->rollback();
        throw;
    }
}

}  // namespace bridge_report::db
