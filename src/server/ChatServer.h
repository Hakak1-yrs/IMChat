#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include "Packet.h"
#include "TcpServer.h"

class Platform;
class ServiceBase;

// 聊天服务端：把网络库收到的字节流按协议切成消息，分发给对应的处理函数，处理结果写回连接。
// 业务规则（账号、好友、消息存储……）全部交给 Platform，这里只负责“会话”这一层：
// 谁在哪条连接上登录了、给在线的人推送新消息、同一账号重复登录时把旧连接挤下线、长时间没有心跳的连接断开。
// 所有函数都在事件循环线程里执行，Platform 也只在这个线程里使用，因此不需要加锁
class ChatServer {
public:
    struct Options {
        std::string ip = "0.0.0.0";
        std::uint16_t port = 9000;
        std::chrono::milliseconds idleTimeout{ 90000 };   // 这么久没有收到任何数据（包括心跳）就断开
        std::chrono::milliseconds idleCheckInterval{ 5000 };
    };

    ChatServer(EventLoop& loop, Platform& platform, Options options);
    ~ChatServer();
    ChatServer(const ChatServer&) = delete;
    ChatServer& operator=(const ChatServer&) = delete;

    bool start(std::string& error);
    std::uint16_t port() const;
    std::size_t onlineCount() const;

private:
    // 一条连接上的会话状态
    struct Session {
        TcpConnection::Ptr connection;
        bool loggedIn = false;
        std::string accountKey;   // "QQ 10001"
    };
    // 最近处理过的客户端消息编号 → 当时的回执。客户端没收到回执而重发同一条消息时，直接返回原回执，不会重复保存
    struct RecentMessages {
        std::unordered_map<std::string, nlohmann::json> acks;
        std::deque<std::string> order;   // 先进先出，超过上限时淘汰最早的
    };
    static constexpr std::size_t kRecentLimit = 256;

    void onConnection(const TcpConnection::Ptr& connection);
    void onMessage(const TcpConnection::Ptr& connection, Buffer& buffer);
    void onClose(const TcpConnection::Ptr& connection);
    void dispatch(Session& session, const Packet& packet);

    void handleLogin(Session& session, const nlohmann::json& request);
    void handleSendMessage(Session& session, const nlohmann::json& request);

    ServiceBase* accountOf(const Session& session) const;
    void send(const TcpConnection::Ptr& connection, MessageType type, nlohmann::json body);
    void sendError(const TcpConnection::Ptr& connection, const nlohmann::json& request, ErrorCode code,
                   const std::string& message);
    void closeIdleConnections();

    EventLoop& loop_;
    Platform& platform_;
    Options options_;
    TcpServer server_;
    EventLoop::TimerId idleTimer_ = 0;
    std::unordered_map<std::uint64_t, Session> sessions_;              // 连接编号 → 会话
    std::unordered_map<std::string, std::uint64_t> online_;            // "QQ 10001" → 连接编号
    std::unordered_map<std::string, RecentMessages> recent_;           // "QQ 10001" → 最近的消息回执
};
