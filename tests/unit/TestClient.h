#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "Packet.h"
#include "SocketOps.h"

// 测试用的阻塞式客户端：连接服务端，按协议收发消息，所有等待都有超时，测试不会卡死
class TestClient {
public:
    explicit TestClient(std::uint16_t port);
    ~TestClient();
    TestClient(const TestClient&) = delete;
    TestClient& operator=(const TestClient&) = delete;

    bool connected() const;
    void sendRaw(const std::string& bytes);
    void send(MessageType type, const nlohmann::json& body = nlohmann::json::object());
    // 下一条消息；超时或连接关闭返回空。好友上下线的推送（PRESENCE）随时可能插进来，不计入，另存在 presences 里
    std::optional<Packet> receive(int timeoutMs = 2000);
    std::vector<nlohmann::json> presences;
    bool closedByPeer(int timeoutMs = 2000);                // 等到服务端关闭连接（期间收到的消息丢弃）

    // 便捷操作：登录并返回 LOGIN_RESP 的正文
    nlohmann::json login(const std::string& service, const std::string& account, const std::string& password = "123456");

private:
    bool waitReadable(int timeoutMs);

    SocketHandle socket_;
    std::string buffer_;
    bool peerClosed_ = false;
};
