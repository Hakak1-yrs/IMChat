#include "PasswordYD.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>

#pragma comment(lib, "bcrypt.lib")   // 链接 Windows CNG 密码学库

namespace {

std::string toHex(const unsigned char* data, size_t length) {
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    for (size_t i = 0; i < length; ++i) {
        hex += digits[data[i] >> 4];
        hex += digits[data[i] & 0x0F];
    }
    return hex;
}

}  // namespace

std::string PasswordYD::sha256Hex(const std::string& data) {
    unsigned char digest[32] = {};
    // BCryptHash 一次完成“打开算法 → 计算摘要”，BCRYPT_SHA256_ALG_HANDLE 是系统预置的算法句柄
    BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
               reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()),
               digest, sizeof(digest));
    return toHex(digest, sizeof(digest));
}

std::string PasswordYD::randomHex(size_t bytes) {
    std::vector<unsigned char> buffer(bytes);
    // 使用系统推荐的密码学安全随机数生成器
    BCryptGenRandom(nullptr, buffer.data(), static_cast<ULONG>(buffer.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return toHex(buffer.data(), buffer.size());
}

std::string PasswordYD::makeRecord(const std::string& plain) {
    std::string salt = randomHex(16);   // 每个账号单独的盐：相同密码的摘要也互不相同
    return salt + "$" + sha256Hex(salt + plain);
}

bool PasswordYD::verify(const std::string& plain, const std::string& record) {
    if (!isRecord(record)) return false;
    std::string salt = record.substr(0, 32);
    std::string expected = record.substr(33);
    std::string actual = sha256Hex(salt + plain);
    // 逐字节比较全部字符，比较耗时与“哪一位不同”无关
    unsigned char diff = 0;
    for (size_t i = 0; i < expected.size(); ++i) diff |= static_cast<unsigned char>(expected[i] ^ actual[i]);
    return diff == 0;
}

bool PasswordYD::isRecord(const std::string& text) {
    if (text.size() != 32 + 1 + 64 || text[32] != '$') return false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (i == 32) continue;
        char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}
