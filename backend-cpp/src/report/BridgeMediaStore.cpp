#include "bridge_report/report/BridgeMediaStore.hpp"

#include "bridge_report/db/BridgeMediaRepository.hpp"

namespace bridge_report::report {

StoredBridgeMedia store_bridge_media(
    const drogon::orm::DbClientPtr& db_client,
    const std::filesystem::path& archive_root,
    const std::string& bridge_id,
    const std::string& slot,
    const archive::ArchivedBridgeMediaFile& archived,
    const std::string& source) {
    BridgeMediaInput input;
    input.slot = slot;
    input.original_file_name = archived.original_file_name;
    input.storage_relative_path = archived.storage_relative_path;
    input.file_extension = archived.file_extension;
    input.file_size_bytes = archived.file_size_bytes;
    input.file_hash = archived.sha256;
    input.source = source;

    db::BridgeMediaSaveOutcome outcome;
    try {
        outcome = db::BridgeMediaRepository(db_client).save(bridge_id, input);
    } catch (...) {
        archive::remove_archived_bridge_media(archive_root, archived.storage_relative_path, archived.sha256);
        throw;
    }

    if (outcome.status != BridgeMediaWriteStatus::Ok) {
        archive::remove_archived_bridge_media(archive_root, archived.storage_relative_path, archived.sha256);
        return {outcome.status, std::nullopt};
    }

    if (outcome.replaced.storage_relative_path.has_value()
        && *outcome.replaced.storage_relative_path != archived.storage_relative_path) {
        archive::remove_archived_bridge_media(archive_root, *outcome.replaced.storage_relative_path,
                                              outcome.replaced.file_hash.value_or(""));
    }
    return {BridgeMediaWriteStatus::Ok, outcome.saved};
}

}  // namespace bridge_report::report
