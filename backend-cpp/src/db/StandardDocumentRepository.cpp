#include "bridge_report/db/StandardDocumentRepository.hpp"

#include <map>
#include <memory>
#include <utility>

#include "bridge_report/db/CommitLatch.hpp"

namespace bridge_report::db {
namespace {

using standards::StandardDocumentRecord;
using standards::StandardVersionRecord;

constexpr const char* kVersionColumns =
    "v.id::text as id, v.document_id::text as document_id, v.label, v.code, v.kind, "
    "v.nature, v.issuer, v.editor, "
    "to_char(v.publish_date, 'YYYY-MM-DD') as publish_date, "
    "to_char(v.effective_date, 'YYYY-MM-DD') as effective_date, "
    "to_char(v.withdrawn_date, 'YYYY-MM-DD') as withdrawn_date, "
    "v.announcement, v.source, v.note, v.archived_file_id::text as archived_file_id, "
    "f.original_file_name, f.storage_relative_path, coalesce(f.file_hash, '') as file_hash, "
    "coalesce(f.file_size_bytes, 0) as file_size_bytes, u.display_name as uploaded_by, "
    "to_char(f.uploaded_at, 'YYYY-MM-DD HH24:MI:SS') as uploaded_at, "
    "to_char(v.updated_at, 'YYYY-MM-DD HH24:MI:SS') as updated_at, "
    "to_char(v.created_at, 'YYYY-MM-DD') as created_date";

constexpr const char* kVersionFrom =
    " from standard_document_versions v"
    " join archived_files f on f.id = v.archived_file_id"
    " left join users u on u.id = v.uploaded_by";

constexpr const char* kVersionOrder =
    " order by coalesce(v.effective_date, v.publish_date, v.created_at::date) desc, v.label";

std::optional<std::string> optional_text(const drogon::orm::Row& row, const char* column) {
    if (row[column].isNull()) return std::nullopt;
    return row[column].as<std::string>();
}

StandardVersionRecord read_version(const drogon::orm::Row& row) {
    StandardVersionRecord version;
    version.id = row["id"].as<std::string>();
    version.document_id = row["document_id"].as<std::string>();
    version.label = row["label"].as<std::string>();
    version.code = row["code"].as<std::string>();
    version.kind = standards::parse_standard_version_kind(row["kind"].as<std::string>())
        .value_or(standards::StandardVersionKind::official);
    version.nature = optional_text(row, "nature");
    version.issuer = optional_text(row, "issuer");
    version.editor = optional_text(row, "editor");
    version.publish_date = optional_text(row, "publish_date");
    version.effective_date = optional_text(row, "effective_date");
    version.withdrawn_date = optional_text(row, "withdrawn_date");
    version.announcement = optional_text(row, "announcement");
    version.source = optional_text(row, "source");
    version.note = optional_text(row, "note");
    version.archived_file_id = row["archived_file_id"].as<std::string>();
    version.original_file_name = row["original_file_name"].as<std::string>();
    version.storage_relative_path = row["storage_relative_path"].as<std::string>();
    version.file_hash = row["file_hash"].as<std::string>();
    version.file_size_bytes = static_cast<std::uintmax_t>(row["file_size_bytes"].as<int64_t>());
    version.uploaded_by = optional_text(row, "uploaded_by");
    version.uploaded_at = row["uploaded_at"].as<std::string>();
    version.updated_at = row["updated_at"].as<std::string>();
    version.created_date = row["created_date"].as<std::string>();
    return version;
}

StandardDocumentRecord read_document(const drogon::orm::Row& row) {
    StandardDocumentRecord document;
    document.id = row["id"].as<std::string>();
    document.name = row["name"].as<std::string>();
    document.english_name = optional_text(row, "english_name");
    document.category = row["category"].as<std::string>();
    return document;
}

/// 可空字段沿用现有写法：传空串，SQL 里 nullif 回 null。
std::string or_empty(const std::optional<std::string>& value) { return value.value_or(""); }

/// 在一个事务里跑 body。body 返回非 Ok 表示业务上不成立，回滚后原样返回；
/// 提交确认丢失时抛 StandardDocumentCommitUncertain。
template <typename Body>
StandardDocumentWriteStatus run_transaction(
    const drogon::orm::DbClientPtr& client, const std::string& context, Body&& body) {
    std::shared_ptr<drogon::orm::Transaction> tx;
    const auto latch = std::make_shared<CommitLatch>();
    try {
        tx = client->newTransaction(latch->callback());
        const auto status = body(*tx);
        if (status != StandardDocumentWriteStatus::Ok) {
            tx->rollback();
            return status;
        }
        tx.reset();
        if (!latch->wait()) throw StandardDocumentCommitUncertain(context);
        return StandardDocumentWriteStatus::Ok;
    } catch (...) {
        if (tx != nullptr) tx->rollback();
        throw;
    }
}

bool lock_document(drogon::orm::Transaction& tx, const std::string& document_id) {
    return !tx.execSqlSync("select 1 from standard_documents where id = $1::uuid for update", document_id).empty();
}

/// 版本所属的规范；版本不存在时为空。
std::optional<std::string> owner_of(drogon::orm::Transaction& tx, const std::string& version_id) {
    const auto rows = tx.execSqlSync(
        "select document_id::text as document_id from standard_document_versions where id = $1::uuid", version_id);
    if (rows.empty()) return std::nullopt;
    return rows[0]["document_id"].as<std::string>();
}

bool name_taken(drogon::orm::Transaction& tx, const std::string& name, const std::string& except_id) {
    return !tx.execSqlSync("select 1 from standard_documents where name = $1 and id::text <> $2",
                           name, except_id).empty();
}

StandardDocumentWriteStatus version_conflict(
    drogon::orm::Transaction& tx, const std::string& document_id,
    const standards::StandardVersionInput& version, const std::string& except_version_id) {
    if (!tx.execSqlSync(
            "select 1 from standard_document_versions "
            "where document_id = $1::uuid and label = $2 and id::text <> $3",
            document_id, version.label, except_version_id).empty()) {
        return StandardDocumentWriteStatus::LabelTaken;
    }
    if (version.kind == standards::StandardVersionKind::official && version.effective_date.has_value() &&
        !tx.execSqlSync(
            "select 1 from standard_document_versions where document_id = $1::uuid and kind = 'official' "
            "and effective_date = $2::date and id::text <> $3",
            document_id, *version.effective_date, except_version_id).empty()) {
        return StandardDocumentWriteStatus::EffectiveDateTaken;
    }
    return StandardDocumentWriteStatus::Ok;
}

std::string insert_archived_file(drogon::orm::Transaction& tx, const StandardPdfFileInput& file) {
    return tx.execSqlSync(
        "insert into archived_files(original_file_name, current_file_name, storage_relative_path, "
        "file_type, file_purpose, file_extension, file_size_bytes, file_hash, source_description) "
        "values($1, $1, $2, '附件', '规范原文', '.pdf', $3, nullif($4, ''), '规范原文上传') "
        "returning id::text as id",
        file.original_file_name, file.storage_relative_path,
        static_cast<int64_t>(file.file_size_bytes), file.sha256)[0]["id"].as<std::string>();
}

void insert_version(drogon::orm::Transaction& tx, const std::string& document_id,
                    const standards::StandardVersionInput& version, const std::string& archived_file_id,
                    const std::string& user_id) {
    tx.execSqlSync(
        "insert into standard_document_versions(document_id, label, code, kind, nature, issuer, editor, "
        "publish_date, effective_date, withdrawn_date, announcement, source, note, archived_file_id, uploaded_by) "
        "values($1::uuid, $2, $3, $4, nullif($5, ''), nullif($6, ''), nullif($7, ''), "
        "nullif($8, '')::date, nullif($9, '')::date, nullif($10, '')::date, "
        "nullif($11, ''), nullif($12, ''), nullif($13, ''), $14::uuid, nullif($15, '')::uuid)",
        document_id, version.label, version.code,
        std::string(standards::standard_version_kind_code(version.kind)),
        or_empty(version.nature), or_empty(version.issuer), or_empty(version.editor),
        or_empty(version.publish_date), or_empty(version.effective_date), or_empty(version.withdrawn_date),
        or_empty(version.announcement), or_empty(version.source), or_empty(version.note),
        archived_file_id, user_id);
}

/// 版本的归档文件（加锁）。版本不存在时为空。
std::optional<std::pair<std::string, StandardFileToRemove>> version_file_for_update(
    drogon::orm::Transaction& tx, const std::string& version_id) {
    const auto rows = tx.execSqlSync(
        "select v.archived_file_id::text as archived_file_id, f.storage_relative_path, "
        "coalesce(f.file_hash, '') as file_hash from standard_document_versions v "
        "join archived_files f on f.id = v.archived_file_id where v.id = $1::uuid for update of v",
        version_id);
    if (rows.empty()) return std::nullopt;
    return std::make_pair(rows[0]["archived_file_id"].as<std::string>(),
                          StandardFileToRemove{rows[0]["storage_relative_path"].as<std::string>(),
                                               rows[0]["file_hash"].as<std::string>()});
}

}  // namespace

StandardDocumentRepository::StandardDocumentRepository(drogon::orm::DbClientPtr client)
    : client_(std::move(client)) {}

std::string StandardDocumentRepository::allocate_document_id() const {
    return client_->execSqlSync("select gen_random_uuid()::text as id")[0]["id"].as<std::string>();
}

std::vector<StandardDocumentRecord> StandardDocumentRepository::list() const {
    const auto rows = client_->execSqlSync(
        "select id::text as id, name, english_name, category from standard_documents order by category, name");
    std::vector<StandardDocumentRecord> documents;
    std::map<std::string, std::size_t> index;
    for (const auto& row : rows) {
        index[row["id"].as<std::string>()] = documents.size();
        documents.push_back(read_document(row));
    }
    // 一次取全部版本：几十部规范，不值得为每部各跑一趟。
    const auto versions = client_->execSqlSync(std::string("select ") + kVersionColumns + kVersionFrom + kVersionOrder);
    for (const auto& row : versions) {
        auto version = read_version(row);
        const auto found = index.find(version.document_id);
        if (found != index.end()) documents[found->second].versions.push_back(std::move(version));
    }
    return documents;
}

std::optional<StandardDocumentRecord> StandardDocumentRepository::find(const std::string& document_id) const {
    const auto rows = client_->execSqlSync(
        "select id::text as id, name, english_name, category from standard_documents where id = $1::uuid",
        document_id);
    if (rows.empty()) return std::nullopt;
    auto document = read_document(rows[0]);
    const auto versions = client_->execSqlSync(
        std::string("select ") + kVersionColumns + kVersionFrom + " where v.document_id = $1::uuid" + kVersionOrder,
        document_id);
    for (const auto& row : versions) document.versions.push_back(read_version(row));
    return document;
}

std::optional<StandardVersionFile> StandardDocumentRepository::find_version_file(const std::string& version_id) const {
    const auto rows = client_->execSqlSync(
        std::string("select ") + kVersionColumns + ", d.name as document_name" + kVersionFrom +
            " join standard_documents d on d.id = v.document_id where v.id = $1::uuid",
        version_id);
    if (rows.empty()) return std::nullopt;
    return StandardVersionFile{read_version(rows[0]), rows[0]["document_name"].as<std::string>()};
}

StandardDocumentWriteOutcome StandardDocumentRepository::create(
    const std::string& document_id, const standards::StandardDocumentInput& document,
    const standards::StandardVersionInput& version, const StandardPdfFileInput& file,
    const std::string& user_id) const {
    const auto status = run_transaction(
        client_, "create document=" + document_id + " file=" + file.storage_relative_path,
        [&](drogon::orm::Transaction& tx) {
            if (name_taken(tx, document.name, "")) return StandardDocumentWriteStatus::NameTaken;
            tx.execSqlSync(
                "insert into standard_documents(id, name, english_name, category) "
                "values($1::uuid, $2, nullif($3, ''), $4)",
                document_id, document.name, or_empty(document.english_name), document.category);
            insert_version(tx, document_id, version, insert_archived_file(tx, file), user_id);
            return StandardDocumentWriteStatus::Ok;
        });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, find(document_id), {}};
}

StandardDocumentWriteOutcome StandardDocumentRepository::update(
    const std::string& document_id, const standards::StandardDocumentInput& document) const {
    const auto status = run_transaction(client_, "update document=" + document_id, [&](drogon::orm::Transaction& tx) {
        if (!lock_document(tx, document_id)) return StandardDocumentWriteStatus::DocumentNotFound;
        if (name_taken(tx, document.name, document_id)) return StandardDocumentWriteStatus::NameTaken;
        tx.execSqlSync(
            "update standard_documents set name = $2, english_name = nullif($3, ''), category = $4, "
            "updated_at = now() where id = $1::uuid",
            document_id, document.name, or_empty(document.english_name), document.category);
        return StandardDocumentWriteStatus::Ok;
    });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, find(document_id), {}};
}

StandardDocumentWriteOutcome StandardDocumentRepository::remove(const std::string& document_id) const {
    std::vector<StandardFileToRemove> files;
    const auto status = run_transaction(client_, "remove document=" + document_id, [&](drogon::orm::Transaction& tx) {
        if (!lock_document(tx, document_id)) return StandardDocumentWriteStatus::DocumentNotFound;
        const auto rows = tx.execSqlSync(
            "select v.archived_file_id::text as archived_file_id, f.storage_relative_path, "
            "coalesce(f.file_hash, '') as file_hash from standard_document_versions v "
            "join archived_files f on f.id = v.archived_file_id where v.document_id = $1::uuid",
            document_id);
        // 先删规范（级联删版本），再删归档行：版本对归档行是 restrict，反过来会被挡住。
        tx.execSqlSync("delete from standard_documents where id = $1::uuid", document_id);
        for (const auto& row : rows) {
            tx.execSqlSync("delete from archived_files where id = $1::uuid", row["archived_file_id"].as<std::string>());
            files.push_back({row["storage_relative_path"].as<std::string>(), row["file_hash"].as<std::string>()});
        }
        return StandardDocumentWriteStatus::Ok;
    });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, std::nullopt, files};
}

StandardDocumentWriteOutcome StandardDocumentRepository::add_version(
    const std::string& document_id, const standards::StandardVersionInput& version,
    const StandardPdfFileInput& file, const std::string& user_id) const {
    const auto status = run_transaction(
        client_, "add version document=" + document_id + " file=" + file.storage_relative_path,
        [&](drogon::orm::Transaction& tx) {
            if (!lock_document(tx, document_id)) return StandardDocumentWriteStatus::DocumentNotFound;
            const auto conflict = version_conflict(tx, document_id, version, "");
            if (conflict != StandardDocumentWriteStatus::Ok) return conflict;
            insert_version(tx, document_id, version, insert_archived_file(tx, file), user_id);
            return StandardDocumentWriteStatus::Ok;
        });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, find(document_id), {}};
}

StandardDocumentWriteOutcome StandardDocumentRepository::update_version(
    const std::string& version_id, const standards::StandardVersionInput& version) const {
    std::string document_id;
    const auto status = run_transaction(client_, "update version=" + version_id, [&](drogon::orm::Transaction& tx) {
        const auto owner = owner_of(tx, version_id);
        if (!owner.has_value()) return StandardDocumentWriteStatus::VersionNotFound;
        document_id = *owner;
        lock_document(tx, document_id);
        if (!version_file_for_update(tx, version_id).has_value()) return StandardDocumentWriteStatus::VersionNotFound;
        const auto conflict = version_conflict(tx, document_id, version, version_id);
        if (conflict != StandardDocumentWriteStatus::Ok) return conflict;
        tx.execSqlSync(
            "update standard_document_versions set label = $2, code = $3, kind = $4, nature = nullif($5, ''), "
            "issuer = nullif($6, ''), editor = nullif($7, ''), publish_date = nullif($8, '')::date, "
            "effective_date = nullif($9, '')::date, withdrawn_date = nullif($10, '')::date, "
            "announcement = nullif($11, ''), source = nullif($12, ''), note = nullif($13, ''), "
            "updated_at = now() where id = $1::uuid",
            version_id, version.label, version.code,
            std::string(standards::standard_version_kind_code(version.kind)),
            or_empty(version.nature), or_empty(version.issuer), or_empty(version.editor),
            or_empty(version.publish_date), or_empty(version.effective_date), or_empty(version.withdrawn_date),
            or_empty(version.announcement), or_empty(version.source), or_empty(version.note));
        return StandardDocumentWriteStatus::Ok;
    });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, find(document_id), {}};
}

StandardDocumentWriteOutcome StandardDocumentRepository::replace_version_file(
    const std::string& version_id, const StandardPdfFileInput& file, const std::string& user_id) const {
    std::string document_id;
    std::vector<StandardFileToRemove> files;
    const auto status = run_transaction(
        client_, "replace version=" + version_id + " new_file=" + file.storage_relative_path,
        [&](drogon::orm::Transaction& tx) {
            const auto owner = owner_of(tx, version_id);
            if (!owner.has_value()) return StandardDocumentWriteStatus::VersionNotFound;
            document_id = *owner;
            lock_document(tx, document_id);
            const auto previous = version_file_for_update(tx, version_id);
            if (!previous.has_value()) return StandardDocumentWriteStatus::VersionNotFound;
            const auto new_file_id = insert_archived_file(tx, file);
            tx.execSqlSync(
                "update standard_document_versions set archived_file_id = $2::uuid, "
                "uploaded_by = nullif($3, '')::uuid, updated_at = now() where id = $1::uuid",
                version_id, new_file_id, user_id);
            tx.execSqlSync("delete from archived_files where id = $1::uuid", previous->first);
            files.push_back(previous->second);
            return StandardDocumentWriteStatus::Ok;
        });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, find(document_id), files};
}

StandardDocumentWriteOutcome StandardDocumentRepository::remove_version(const std::string& version_id) const {
    std::string document_id;
    std::vector<StandardFileToRemove> files;
    const auto status = run_transaction(client_, "remove version=" + version_id, [&](drogon::orm::Transaction& tx) {
        const auto owner = owner_of(tx, version_id);
        if (!owner.has_value()) return StandardDocumentWriteStatus::VersionNotFound;
        document_id = *owner;
        lock_document(tx, document_id);
        const auto previous = version_file_for_update(tx, version_id);
        if (!previous.has_value()) return StandardDocumentWriteStatus::VersionNotFound;
        tx.execSqlSync("delete from standard_document_versions where id = $1::uuid", version_id);
        tx.execSqlSync("delete from archived_files where id = $1::uuid", previous->first);
        files.push_back(previous->second);
        return StandardDocumentWriteStatus::Ok;
    });
    if (status != StandardDocumentWriteStatus::Ok) return {status, std::nullopt, {}};
    return {status, find(document_id), files};
}

}  // namespace bridge_report::db
