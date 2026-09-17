#pragma once

#include <string>
#include <string_view>

namespace bridge_report::archive {

/**
 * @brief 按文件头判断上传内容是不是图片，以及是哪一种。
 *
 * 只信内容，不信扩展名：浏览器给的文件名是用户可以随便写的，而写进归档的扩展名会被
 * 后续所有环节当作事实。
 *
 * @return 规范扩展名（".jpg" ".png" ".gif" ".bmp" ".webp" ".tiff"）；认不出返回空串。
 */
std::string detect_image_extension(std::string_view content);

/// 扩展名和识别结果是否相符。.jpeg 算 .jpg，.tif 算 .tiff。
bool image_extension_matches(std::string_view extension, std::string_view detected);

/// 内容的 SHA-256，小写十六进制。归档去重和删除前的比对都用它。
std::string sha256_hex(std::string_view content);

}  // namespace bridge_report::archive
