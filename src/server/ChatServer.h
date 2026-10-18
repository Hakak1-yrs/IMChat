#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include "Packet.h"
#include "ServiceType.h"
#include "TcpServer.h"

class Message;
class Platform;
class ServiceBase;

// 聊天服务端：把网络库收到的字节流按协议切成消息，分发给对应的处理函数，处理结果写回连接。
// 业务规则（账号、好友、群、消息存储……）全部交给 Platform，这里只负责“会话”这一层：
// 谁在哪条连接上登录了、给在线的人推送新消息（私信推给对方，群消息推给所有在线成员）、
// 客户端上线后补发离线期间的消息、已读回执、同一账号重复登录时把旧连接挤下线、长时间没有心跳的连接断开。
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
    static constexpr int kSyncDefaultLimit = 200;   // 一次同步最多返回的消息条数（客户端可以要求更少）
    static constexpr int kSyncMaxLimit = 500;

    // 通用请求（CALL_REQ）的处理函数：返回 data；业务上的失败抛出 ApiError，回给客户端 ok:false 和原因。
    // 新增一种操作只需要在 registerApi 里登记一项，不必新增消息类型
    using ApiHandler = std::function<nlohmann::json(Session& session, ServiceBase& me, const nlohmann::json& args)>;
    struct ApiError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };
    void registerApi();
    nlohmann::json contactsList(ServiceBase& me);
    nlohmann::json groupsList(ServiceBase& me);
    nlohmann::json readsGet(ServiceBase& me);
    nlohmann::json revokeMessage(ServiceBase& me, const nlohmann::json& args);
    nlohmann::json socialGet(ServiceBase& me);
    nlohmann::json profileGet(ServiceBase& me, const nlohmann::json& args);
    nlohmann::json requestFriend(ServiceBase& me, const nlohmann::json& args);
    nlohmann::json respondRequest(ServiceBase& me, const nlohmann::json& args);
    nlohmann::json removeFriend(ServiceBase& me, const nlohmann::json& args);
    nlohmann::json setRemark(ServiceBase& me, const nlohmann::json& args);
    nlohmann::json setBlocked(ServiceBase& me, const nlohmann::json& args);
    nlohmann::json setVerification(ServiceBase& me, const nlohmann::json& args);
    void notifyChanged(ServiceType type, const std::string& id, std::initializer_list<const char*> topics);   // 推送 DATA_CHANGED
    void persist();   // 改了好友、群等内存数据后立即写回文件（消息类记录本来就是即时追加的）

    void onConnection(const TcpConnection::Ptr& connection);
    void onMessage(const TcpConnection::Ptr& connection, Buffer& buffer);
    void onClose(const TcpConnection::Ptr& connection);
    void dispatch(Session& session, const Packet& packet);

    void handleLogin(Session& session, const nlohmann::json& request);
    void handleSendMessage(Session& session, const nlohmann::json& request);
    void handleSync(Session& session, const nlohmann::json& request);
    void handleRead(Session& session, const nlohmann::json& request);
    void handleCall(Session& session, const nlohmann::json& request);

    nlohmann::json messageJson(const Message& message) const;   // 推送和同步共用的消息格式
    void notifyPresence(const std::string& accountKey, bool online);   // 通知在线的好友：我上线 / 下线了
    bool isOnline(const std::string& accountKey) const;
    ServiceBase* accountOf(const Session& session) const;
    void pushTo(const std::string& accountKey, MessageType type, const nlohmann::json& body);   // 对方在线才发
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
    std::unordered_map<std::string, ApiHandler> api_;                  // op → 处理函数
};
