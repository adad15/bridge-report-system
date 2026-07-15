#include <string>

#include <gtest/gtest.h>

#include "bridge_report/auth/PasswordHash.hpp"

namespace auth = bridge_report::auth;

TEST(PasswordHashTest, HashAndVerifyRoundtrip) {
    // 测试用低迭代次数，避免拖慢测试；格式与默认参数完全一致。
    const auto hash = auth::hash_password("admin123", 1000);

    EXPECT_EQ(hash.rfind("pbkdf2_sha256$1000$", 0), 0u);
    EXPECT_TRUE(auth::verify_password("admin123", hash));
    EXPECT_FALSE(auth::verify_password("admin124", hash));
    EXPECT_FALSE(auth::verify_password("", hash));
}

TEST(PasswordHashTest, SaltMakesHashesUnique) {
    const auto first = auth::hash_password("same-password", 1000);
    const auto second = auth::hash_password("same-password", 1000);

    EXPECT_NE(first, second);
    EXPECT_TRUE(auth::verify_password("same-password", first));
    EXPECT_TRUE(auth::verify_password("same-password", second));
}

TEST(PasswordHashTest, MalformedStoredHashFailsVerificationWithoutThrowing) {
    EXPECT_FALSE(auth::verify_password("x", ""));
    EXPECT_FALSE(auth::verify_password("x", "plaintext"));
    EXPECT_FALSE(auth::verify_password("x", "md5$1000$00$00"));
    EXPECT_FALSE(auth::verify_password("x", "pbkdf2_sha256$abc$00$00"));
    EXPECT_FALSE(auth::verify_password("x", "pbkdf2_sha256$-5$00$00"));
    EXPECT_FALSE(auth::verify_password("x", "pbkdf2_sha256$1000$zz$00"));
    EXPECT_FALSE(auth::verify_password("x", "pbkdf2_sha256$1000$00$abcd"));
}

TEST(PasswordHashTest, SessionTokenIs64HexCharsAndRandom) {
    const auto first = auth::generate_session_token();
    const auto second = auth::generate_session_token();

    EXPECT_EQ(first.size(), 64u);
    EXPECT_NE(first, second);
    EXPECT_EQ(first.find_first_not_of("0123456789abcdef"), std::string::npos);
}

TEST(PasswordHashTest, Sha256HexMatchesKnownVector) {
    // SHA-256("abc") 的标准测试向量。
    EXPECT_EQ(
        auth::sha256_hex("abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
    EXPECT_EQ(
        auth::sha256_hex(""),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    );
}
