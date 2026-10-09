#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "bridge_report/standards/StandardDocumentModels.hpp"

namespace bridge_report::db {

/// 提交结果不确定：服务器可能已经提交，只是确认没送回来。此时任何文件都不能删。
class StandardDocumentCommitUncertain : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class StandardDocumentWriteStatus {
    Ok,
    DocumentNotFound,
    VersionNotFound,
    NameTaken,
    LabelTaken,
    EffectiveDateTaken,
};

/// 已写进归档目录、还没入库的新原文。
struct StandardPdfFileInput {
    std::string original_file_name;
    std::string storage_relative_path;
    std::uintmax_t file_size_bytes{0};
    std::string sha256;
};

/// 事务提交之后要从磁盘删掉的文件。
struct StandardFileToRemove {
    std::string storage_relative_path;
    std::string sha256;
};

struct StandardDocumentWriteOutcome {
    StandardDocumentWriteStatus status{StandardDocumentWriteStatus::Ok};
    /// 写完之后的规范；删除整部规范时为空。
    std::optional<standards::StandardDocumentRecord> document;
    std::vector<StandardFileToRemove> files_to_remove;
};

/// 原文内容接口要的东西：版本记录，加上拼下载文件名用的规范名称。
struct StandardVersionFile {
    standards::StandardVersionRecord version;
    std::string document_name;
};

/**
 * @brief 规范原文库的读写（设计 §6、§7）。
 *
 * 文件本身在 archived_files 里。和桥梁图件一样：磁盘上的旧文件由调用方在事务提交之后删，
 * 这里只把「要删哪些」交回去；提交确认丢失时抛 StandardDocumentCommitUncertain。
 * 同一部规范的版本写入都先锁住规范那一行，重名和实施日期的检查才可靠。
 */
class StandardDocumentRepository {
public:
    explicit StandardDocumentRepository(drogon::orm::DbClientPtr client);

    /// 新规范的 id。路由要先拿到它，才能把原文写进 `standards/<id>/`。
    std::string allocate_document_id() const;

    std::vector<standards::StandardDocumentRecord> list() const;
    std::optional<standards::StandardDocumentRecord> find(const std::string& document_id) const;
    std::optional<StandardVersionFile> find_version_file(const std::string& version_id) const;

    StandardDocumentWriteOutcome create(
        const std::string& document_id,
        const standards::StandardDocumentInput& document,
        const standards::StandardVersionInput& version,
        const StandardPdfFileInput& file,
        const std::string& user_id) const;
    StandardDocumentWriteOutcome update(
        const std::string& document_id, const standards::StandardDocumentInput& document) const;
    StandardDocumentWriteOutcome remove(const std::string& document_id) const;

    StandardDocumentWriteOutcome add_version(
        const std::string& document_id,
        const standards::StandardVersionInput& version,
        const StandardPdfFileInput& file,
        const std::string& user_id) const;
    StandardDocumentWriteOutcome update_version(
        const std::string& version_id, const standards::StandardVersionInput& version) const;
    StandardDocumentWriteOutcome replace_version_file(
        const std::string& version_id, const StandardPdfFileInput& file, const std::string& user_id) const;
    StandardDocumentWriteOutcome remove_version(const std::string& version_id) const;

private:
    drogon::orm::DbClientPtr client_;
};

}  // namespace bridge_report::db
