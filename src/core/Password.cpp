#include "Password.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")   // 链接 Windows CNG 密码学库
#endif

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

std::string Password::sha256Hex(const std::string& data) {
    unsigned char digest[32] = {};
    // BCryptHash 一次完成“打开算法 → 计算摘要”，BCRYPT_SHA256_ALG_HANDLE 是系统预置的算法句柄
    BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
               reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()),
               digest, sizeof(digest));
    return toHex(digest, sizeof(digest));
}

std::string Password::hmacSha256Hex(const std::string& key, const std::string& data) {
    unsigned char digest[32] = {};
    BCryptHash(BCRYPT_HMAC_SHA256_ALG_HANDLE, reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())), static_cast<ULONG>(key.size()),
               reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()), digest, sizeof(digest));
    return toHex(digest, sizeof(digest));
}

bool Password::equalsConstantTime(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

std::string Password::randomHex(size_t bytes) {
    std::vector<unsigned char> buffer(bytes);
    // 使用系统推荐的密码学安全随机数生成器
    BCryptGenRandom(nullptr, buffer.data(), static_cast<ULONG>(buffer.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return toHex(buffer.data(), buffer.size());
}

std::string Password::makeRecord(const std::string& plain) {
    std::string salt = randomHex(16);   // 每个账号单独的盐：相同密码的摘要也互不相同
    return salt + "$" + sha256Hex(salt + plain);
}

bool Password::verify(const std::string& plain, const std::string& record) {
    if (!isRecord(record)) return false;
    std::string salt = record.substr(0, 32);
    std::string expected = record.substr(33);
    return equalsConstantTime(expected, sha256Hex(salt + plain));
}

bool Password::isRecord(const std::string& text) {
    if (text.size() != 32 + 1 + 64 || text[32] != '$') return false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (i == 32) continue;
        char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}
