#pragma once
#include <string>

// 密码工具类：不保存明文密码，而是保存“随机盐 + SHA-256 摘要”。
// 摘要计算直接调用 Windows 自带的 BCrypt（CNG）密码学接口，不自己实现算法。
// 存储格式：盐(32位十六进制)$摘要(64位十六进制)
class Password {
public:
    static std::string makeRecord(const std::string& plain);                      // 明文 → 存储记录
    static bool verify(const std::string& plain, const std::string& record);      // 校验密码
    static bool isRecord(const std::string& text);                                // 是否已经是加密记录（用于兼容旧数据）

    static std::string randomHex(size_t bytes);   // 密码学安全的随机数（盐、服务端密钥）
    static std::string hmacSha256Hex(const std::string& key, const std::string& data);   // 登录令牌的签名
    static bool equalsConstantTime(const std::string& a, const std::string& b);          // 比较耗时与“哪一位不同”无关

private:
    static std::string sha256Hex(const std::string& data);
};
