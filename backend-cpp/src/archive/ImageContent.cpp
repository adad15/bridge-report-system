#include "bridge_report/archive/ImageContent.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <memory>
#include <sstream>

#include <openssl/evp.h>

namespace bridge_report::archive {
namespace {

bool has_jpeg_head(const unsigned char* bytes, std::size_t count) {
    return count >= 4 && bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff;
}

/// 只看文件头，认不出就返回空串。JPEG 另外看结尾，由调用方判断。
std::string detect_by_head(const unsigned char* bytes, std::size_t count) {
    const std::array<unsigned char, 8> png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (count >= 8 && std::equal(png.begin(), png.end(), bytes)) return ".png";
    if (count >= 6 && ((std::equal(bytes, bytes + 6, reinterpret_cast<const unsigned char*>("GIF87a")))
        || (std::equal(bytes, bytes + 6, reinterpret_cast<const unsigned char*>("GIF89a"))))) return ".gif";
    if (count >= 14 && bytes[0] == 'B' && bytes[1] == 'M') return ".bmp";
    if (count >= 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F'
        && bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P') return ".webp";
    if (count >= 4 && ((bytes[0] == 'I' && bytes[1] == 'I' && bytes[2] == 42 && bytes[3] == 0)
        || (bytes[0] == 'M' && bytes[1] == 'M' && bytes[2] == 0 && bytes[3] == 42))) return ".tiff";
    return {};
}

std::string lowercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

using DigestContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

}  // namespace

std::string detect_image_extension(std::string_view content) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(content.data());
    const auto count = content.size();
    // JPEG 要头尾都对：只认头的话，一个被截断的 JPEG 会被当成完好的图片写进归档。
    if (has_jpeg_head(bytes, count) && count >= 6
        && bytes[count - 2] == 0xff && bytes[count - 1] == 0xd9) {
        return ".jpg";
    }
    return detect_by_head(bytes, count);
}

bool image_extension_matches(std::string_view extension, std::string_view detected) {
    const auto given = lowercase(extension);
    if (detected == ".jpg") return given == ".jpg" || given == ".jpeg";
    if (detected == ".tiff") return given == ".tif" || given == ".tiff";
    return given == detected;
}

std::string sha256_hex(std::string_view content) {
    DigestContext context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (context == nullptr) return {};
    if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) return {};
    if (EVP_DigestUpdate(context.get(), content.data(), content.size()) != 1) return {};

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1) return {};

    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (unsigned int index = 0; index < length; ++index) {
        text << std::setw(2) << static_cast<int>(digest[index]);
    }
    return text.str();
}

}  // namespace bridge_report::archive
