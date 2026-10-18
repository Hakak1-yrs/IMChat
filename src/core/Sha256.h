#pragma once
#include <array>
#include <cstdint>
#include <string>

// SHA-256 与 HMAC-SHA256（FIPS 180-4 / RFC 2104）的可移植实现。
// Windows 上 Password 直接调用系统的 CNG；其他平台没有统一的系统接口，用这里的实现。
// 单元测试用标准测试向量验证它，并在 Windows 上与 CNG 的结果比对
class Sha256 {
public:
    using Digest = std::array<std::uint8_t, 32>;

    static Digest hash(const std::string& data);
    static Digest hmac(const std::string& key, const std::string& data);
    static std::string hex(const Digest& digest);

private:
    Sha256();
    void update(const std::uint8_t* data, std::size_t size);
    Digest finish();
    void transform(const std::uint8_t* block);

    std::uint32_t state_[8];
    std::uint8_t buffer_[64];
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0;   // 已输入的字节数
};
