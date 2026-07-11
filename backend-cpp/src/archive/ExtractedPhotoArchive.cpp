#include "bridge_report/archive/ExtractedPhotoArchive.hpp"

#include "bridge_report/archive/ArchivePaths.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>

#include <openssl/evp.h>

namespace bridge_report::archive {
namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string detect_image_extension(const std::filesystem::path& path) {
    std::array<unsigned char, 16> bytes{};
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const auto count = input.gcount();
    if (!input.eof() && input.bad()) throw PhotoArchiveError("unable to read temporary photo: " + path.string());
    if (count >= 4 && bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff) {
        std::ifstream tail(path, std::ios::binary);
        tail.seekg(-2, std::ios::end);
        unsigned char end[2]{};
        tail.read(reinterpret_cast<char*>(end), 2);
        if (tail && end[0] == 0xff && end[1] == 0xd9) return ".jpg";
    }
    const std::array<unsigned char, 8> png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (count >= 8 && std::equal(png.begin(), png.end(), bytes.begin())) return ".png";
    if (count >= 6 && ((std::equal(bytes.begin(), bytes.begin() + 6, reinterpret_cast<const unsigned char*>("GIF87a")))
        || (std::equal(bytes.begin(), bytes.begin() + 6, reinterpret_cast<const unsigned char*>("GIF89a"))))) return ".gif";
    if (count >= 14 && bytes[0] == 'B' && bytes[1] == 'M') return ".bmp";
    if (count >= 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F'
        && bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P') return ".webp";
    if (count >= 4 && ((bytes[0] == 'I' && bytes[1] == 'I' && bytes[2] == 42 && bytes[3] == 0)
        || (bytes[0] == 'M' && bytes[1] == 'M' && bytes[2] == 0 && bytes[3] == 42))) return ".tiff";
    throw PhotoArchiveError("temporary photo is not a supported image: " + path.string());
}

bool extension_matches(const std::string& extension, const std::string& detected) {
    if (detected == ".jpg") return extension == ".jpg" || extension == ".jpeg";
    if (detected == ".tiff") return extension == ".tif" || extension == ".tiff";
    return extension == detected;
}

std::string sha256_file(const std::filesystem::path& path) {
    using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        throw PhotoArchiveError("unable to initialize SHA-256");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) throw PhotoArchiveError("unable to open file for SHA-256: " + path.string());
    std::array<char, 8192> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (input.gcount() > 0
            && EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(input.gcount())) != 1) {
            throw PhotoArchiveError("unable to hash temporary photo");
        }
    }
    if (input.bad()) throw PhotoArchiveError("unable to read file for SHA-256: " + path.string());
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1) {
        throw PhotoArchiveError("unable to finalize SHA-256");
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int index = 0; index < length; ++index) output << std::setw(2) << static_cast<int>(digest[index]);
    return output.str();
}

ArchivedPhotoFile archive_one(const Json::Value& photo, const PhotoArchiveContext& context) {
    const auto candidate_id = photo["candidate_id"].asString();
    const auto temporary_name = photo["extracted_file"]["temporary_file_name"].asString();
    if (candidate_id.empty() || temporary_name.empty()) throw PhotoArchiveError("photo candidate metadata is incomplete");
    const std::filesystem::path temporary_relative(temporary_name);
    if (!is_safe_archive_relative_path(temporary_relative)) throw PhotoArchiveError("unsafe temporary photo path");

    std::filesystem::path source;
    try { source = resolve_path_under_root(context.staging_root, temporary_relative); }
    catch (const std::exception& error) { throw PhotoArchiveError(error.what()); }
    if (!std::filesystem::is_regular_file(source)) throw PhotoArchiveError("temporary photo does not exist: " + temporary_name);

    const auto extension = lowercase(source.extension().string());
    const auto detected = detect_image_extension(source);
    if (!extension_matches(extension, detected)) throw PhotoArchiveError("photo extension does not match its content");
    const auto hash = sha256_file(source);
    const auto current_name = sanitize_path_part(candidate_id) + "_" + hash.substr(0, 12) + extension;
    const auto relative = build_import_photo_relative_path(
        context.bridge_system_number, context.bridge_name, context.inspection_year,
        context.import_record_system_number, context.import_name, candidate_id, current_name);
    const auto destination = resolve_path_under_root(context.archive_root, relative);
    std::filesystem::create_directories(destination.parent_path());
    bool created_by_batch = false;
    if (std::filesystem::exists(destination)) {
        if (!std::filesystem::is_regular_file(destination) || sha256_file(destination) != hash) {
            throw PhotoArchiveError("existing archived photo does not match expected SHA-256");
        }
    } else {
        const auto temporary_destination = destination.string() + ".tmp";
        std::error_code cleanup_error;
        std::filesystem::remove(temporary_destination, cleanup_error);
        try {
            std::filesystem::copy_file(source, temporary_destination);
            if (sha256_file(temporary_destination) != hash) {
                throw PhotoArchiveError("archived copy SHA-256 does not match source");
            }
            std::filesystem::rename(temporary_destination, destination);
            created_by_batch = true;
        } catch (...) {
            std::filesystem::remove(temporary_destination, cleanup_error);
            if (std::filesystem::exists(destination) && std::filesystem::is_regular_file(destination)
                && sha256_file(destination) == hash) {
                created_by_batch = false;
            } else {
                throw;
            }
        }
    }
    return ArchivedPhotoFile{candidate_id, source.filename().string(), current_name, relative, extension,
                             std::filesystem::file_size(destination), hash, created_by_batch};
}

}  // namespace

ArchivedPhotoBatch archive_extracted_photos(const Json::Value& data, const PhotoArchiveContext& context) {
    ArchivedPhotoBatch batch{data, {}};
    if (!batch.data["photos"].isArray()) throw PhotoArchiveError("photos must be an array");
    try {
        for (Json::ArrayIndex index = 0; index < batch.data["photos"].size(); ++index) {
            auto file = archive_one(batch.data["photos"][index], context);
            batch.data["photos"][index]["extracted_file"]["archive_relative_path"] = file.storage_relative_path.generic_string();
            batch.files.push_back(std::move(file));
        }
        return batch;
    } catch (...) {
        cleanup_archived_photo_batch(context.archive_root, batch);
        throw;
    }
}

void cleanup_archived_photo_batch(const std::filesystem::path& archive_root, const ArchivedPhotoBatch& batch) noexcept {
    for (const auto& file : batch.files) {
        if (!file.created_by_batch) {
            continue;
        }
        try {
            const auto path = resolve_path_under_root(archive_root, file.storage_relative_path);
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error || sha256_file(path) != file.sha256) {
                continue;
            }
            std::filesystem::remove(path, error);
            auto directory = path.parent_path();
            const auto normalized_root = std::filesystem::weakly_canonical(archive_root);
            while (!error && directory != normalized_root && std::filesystem::is_empty(directory, error)) {
                std::filesystem::remove(directory, error);
                directory = directory.parent_path();
            }
        } catch (...) {
        }
    }
}

}  // namespace bridge_report::archive
