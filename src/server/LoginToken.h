#pragma once
#include <chrono>
#include <string>

// 登录令牌：用密码登录成功后服务端签发，之后凭它登录同一个用户名下的账号。用在两处：
// - 一处登录、多个服务同时在线：用 QQ 号登录后，客户端凭令牌再登录本人的微信、微博账号，不必再输密码；
// - 断线重连：客户端用令牌重新登录，内存里不必一直保存明文密码。
//
// 格式："用户编号.过期时间.签名"，签名 = HMAC-SHA256(服务端密钥, "用户编号.过期时间")。
// 服务端不保存令牌：验证时重新计算签名比对即可。密钥保存在数据目录里，服务端重启后已签发的令牌仍然有效；
// 没有密钥就伪造不出签名，改动用户编号或过期时间都会让签名对不上
class LoginToken {
public:
    explicit LoginToken(std::string secret);

    std::string issue(int userId, std::chrono::seconds lifetime = std::chrono::hours(24 * 7)) const;
    bool verify(const std::string& token, int& userId) const;   // 签名正确且没有过期时返回 true

private:
    std::string sign(const std::string& payload) const;

    std::string secret_;
};
