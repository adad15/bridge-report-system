#pragma once

#include <string>

namespace bridge_report::auth {

/**
 * @brief 口令哈希与会话令牌的纯密码学工具（OpenSSL），不接触数据库。
 *
 * 哈希存储格式：`pbkdf2_sha256$<iterations>$<salt_hex>$<hash_hex>`。
 * 会话令牌是 32 随机字节的 hex（64 字符）；数据库只存 sha256_hex(token)，
 * 明文令牌只在登录响应里出现一次。
 */

/// PBKDF2-HMAC-SHA256（16 字节随机盐、32 字节导出密钥）。
std::string hash_password(const std::string& password, int iterations = 100000);

/// 常数时间比较；stored_hash 格式非法时按验证失败处理，不抛异常。
bool verify_password(const std::string& password, const std::string& stored_hash);

/// 生成 32 随机字节的 hex 会话令牌。
std::string generate_session_token();

/// SHA-256 的 hex 摘要（用于会话令牌入库前的单向变换）。
std::string sha256_hex(const std::string& text);

}  // namespace bridge_report::auth
