#include "bridge_report/auth/PasswordHash.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <optional>
#include <stdexcept>
#include <vector>

namespace bridge_report::auth {
namespace {

constexpr int kSaltBytes = 16;
constexpr int kDerivedKeyBytes = 32;

std::string to_hex(const unsigned char* data, size_t length) {
    static const char* digits = "0123456789abcdef";
    std::string result;
    result.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        result.push_back(digits[data[i] >> 4]);
        result.push_back(digits[data[i] & 0x0f]);
    }
    return result;
}

std::optional<std::vector<unsigned char>> from_hex(const std::string& text) {
    if (text.empty() || text.size() % 2 != 0) {
        return std::nullopt;
    }
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<unsigned char> bytes;
    bytes.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2) {
        const int high = nibble(text[i]);
        const int low = nibble(text[i + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return bytes;
}

std::vector<unsigned char> derive_key(
    const std::string& password,
    const std::vector<unsigned char>& salt,
    int iterations
) {
    std::vector<unsigned char> key(kDerivedKeyBytes);
    const int ok = PKCS5_PBKDF2_HMAC(
        password.c_str(),
        static_cast<int>(password.size()),
        salt.data(),
        static_cast<int>(salt.size()),
        iterations,
        EVP_sha256(),
        static_cast<int>(key.size()),
        key.data()
    );
    if (ok != 1) {
        throw std::runtime_error("PBKDF2 密钥导出失败");
    }
    return key;
}

}  // namespace

std::string hash_password(const std::string& password, int iterations) {
    if (iterations <= 0) {
        throw std::invalid_argument("PBKDF2 迭代次数必须为正");
    }
    std::vector<unsigned char> salt(kSaltBytes);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        throw std::runtime_error("生成口令盐失败");
    }
    const auto key = derive_key(password, salt, iterations);
    return "pbkdf2_sha256$" + std::to_string(iterations) + "$"
        + to_hex(salt.data(), salt.size()) + "$" + to_hex(key.data(), key.size());
}

bool verify_password(const std::string& password, const std::string& stored_hash) {
    // 拆 4 段：算法$迭代$盐$哈希。任何一段不合法都按验证失败处理。
    const auto first = stored_hash.find('$');
    if (first == std::string::npos) return false;
    const auto second = stored_hash.find('$', first + 1);
    if (second == std::string::npos) return false;
    const auto third = stored_hash.find('$', second + 1);
    if (third == std::string::npos) return false;

    if (stored_hash.substr(0, first) != "pbkdf2_sha256") return false;

    int iterations = 0;
    try {
        iterations = std::stoi(stored_hash.substr(first + 1, second - first - 1));
    } catch (const std::exception&) {
        return false;
    }
    if (iterations <= 0) return false;

    const auto salt = from_hex(stored_hash.substr(second + 1, third - second - 1));
    const auto expected = from_hex(stored_hash.substr(third + 1));
    if (!salt.has_value() || !expected.has_value() || expected->size() != kDerivedKeyBytes) {
        return false;
    }

    const auto actual = derive_key(password, *salt, iterations);
    return CRYPTO_memcmp(actual.data(), expected->data(), expected->size()) == 0;
}

std::string generate_session_token() {
    unsigned char bytes[32];
    if (RAND_bytes(bytes, static_cast<int>(sizeof(bytes))) != 1) {
        throw std::runtime_error("生成会话令牌失败");
    }
    return to_hex(bytes, sizeof(bytes));
}

std::string sha256_hex(const std::string& text) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0;
    if (EVP_Digest(text.data(), text.size(), digest, &digest_length, EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("SHA-256 摘要计算失败");
    }
    return to_hex(digest, digest_length);
}

}  // namespace bridge_report::auth
