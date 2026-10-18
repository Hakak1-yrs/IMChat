#include "Password.h"
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")   // 链接 Windows CNG 密码学库
#endif
#else
#include <fstream>
#include <random>
#include "Sha256.h"
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

#ifdef _WIN32
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

void Password::randomBytes(unsigned char* buffer, size_t size) {
    // 使用系统推荐的密码学安全随机数生成器
    BCryptGenRandom(nullptr, buffer, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}
#else
// 其他平台：摘要用可移植的 SHA-256 实现，随机数读系统的 /dev/urandom
std::string Password::sha256Hex(const std::string& data) { return Sha256::hex(Sha256::hash(data)); }

std::string Password::hmacSha256Hex(const std::string& key, const std::string& data) { return Sha256::hex(Sha256::hmac(key, data)); }

void Password::randomBytes(unsigned char* buffer, size_t size) {
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size))) return;
    std::random_device device;   // 没有 /dev/urandom 的系统
    for (size_t i = 0; i < size; ++i) buffer[i] = static_cast<unsigned char>(device());
}
#endif

bool Password::equalsConstantTime(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

std::string Password::randomHex(size_t bytes) {
    std::vector<unsigned char> buffer(bytes);
    randomBytes(buffer.data(), buffer.size());
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
