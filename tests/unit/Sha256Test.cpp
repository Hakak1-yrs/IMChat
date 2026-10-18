#include <gtest/gtest.h>
#include "Password.h"
#include "Sha256.h"

// 可移植 SHA-256 / HMAC-SHA256 的标准测试向量（FIPS 180-4 示例、RFC 4231），
// 并与 Password 的结果比对（Windows 上 Password 用的是系统 CNG，等于拿两份独立实现互相验证）

TEST(Sha256Test, MatchesStandardVectors) {
    EXPECT_EQ(Sha256::hex(Sha256::hash("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(Sha256::hex(Sha256::hash("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(Sha256::hex(Sha256::hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(Sha256::hex(Sha256::hash(std::string(1000000, 'a'))),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256Test, HmacMatchesRfc4231) {
    EXPECT_EQ(Sha256::hex(Sha256::hmac(std::string(20, '\x0b'), "Hi There")),
              "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    EXPECT_EQ(Sha256::hex(Sha256::hmac("Jefe", "what do ya want for nothing?")),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    // 密钥比分组长（131 字节）：先对密钥做哈希
    EXPECT_EQ(Sha256::hex(Sha256::hmac(std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First")),
              "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST(Sha256Test, AgreesWithPasswordHashing) {
    EXPECT_EQ(Password::hmacSha256Hex("key", "The quick brown fox jumps over the lazy dog"),
              Sha256::hex(Sha256::hmac("key", "The quick brown fox jumps over the lazy dog")));
    const std::string record = Password::makeRecord("123456");
    EXPECT_TRUE(Password::verify("123456", record));
    EXPECT_FALSE(Password::verify("654321", record));
    // 演示数据里的密码记录是在 Windows 上用 CNG 生成的，在其他平台上也必须能验证通过
    EXPECT_EQ(record.substr(33), Sha256::hex(Sha256::hash(record.substr(0, 32) + "123456")));
}
