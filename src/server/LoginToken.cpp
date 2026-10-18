#include "LoginToken.h"
#include "Password.h"

namespace {

long long nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

LoginToken::LoginToken(std::string secret) : secret_(std::move(secret)) {}

std::string LoginToken::sign(const std::string& payload) const { return Password::hmacSha256Hex(secret_, payload); }

std::string LoginToken::issue(int userId, std::chrono::seconds lifetime) const {
    const std::string payload = std::to_string(userId) + "." + std::to_string(nowSeconds() + lifetime.count());
    return payload + "." + sign(payload);
}

bool LoginToken::verify(const std::string& token, int& userId) const {
    const std::size_t first = token.find('.');
    const std::size_t second = first == std::string::npos ? std::string::npos : token.find('.', first + 1);
    if (second == std::string::npos) return false;
    const std::string payload = token.substr(0, second);
    if (!Password::equalsConstantTime(sign(payload), token.substr(second + 1))) return false;
    try {
        std::size_t used = 0;
        const int id = std::stoi(payload.substr(0, first), &used);
        if (used != first) return false;
        if (std::stoll(payload.substr(first + 1)) < nowSeconds()) return false;   // 过期
        userId = id;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
