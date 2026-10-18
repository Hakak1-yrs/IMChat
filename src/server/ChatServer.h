#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "LoginToken.h"
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
        std::string tokenSecret;   // 登录令牌的签名密钥；为空时随机生成（服务端重启后旧令牌失效）
        // 另开的 I/O 线程数。为 0 时接受连接、收发和业务处理都在一个线程里；
        // 大于 0 时收发、切帧、解析 JSON 在 I/O 线程里做，业务处理仍在一个线程里（Platform 不需要加锁）
        int ioThreads = 0;
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
    nlohmann::json groupsAll(ServiceBase& me);
    nlohmann::json createGroup(ServiceBase& me, const nlohmann::json& args);
    nlohmann::json execGroup(ServiceBase& me, const nlohmann::json& args);
    // 群发生变化后：把新产生的群消息（lastSeq 之后的系统提示、公告）推给在线成员，
    // 再通知 affected 和现在的成员、申请人重新获取群数据
    void afterGroupChange(ServiceBase& me, int groupNo, std::set<std::string> affected, int lastSeq);
    nlohmann::json momentsList(ServiceBase& me, const nlohmann::json& args);
    // 点赞、评论、删除某条动态：change 执行修改，成功后通知能看到这条动态的人
    nlohmann::json changeMoment(ServiceBase& me, int seq, const std::function<bool(std::string&)>& change);
    void notifyMomentAudience(ServiceBase& me, const std::string& author);   // 向 author 及其好友推送 DATA_CHANGED moments
    void notifyChanged(ServiceType type, const std::string& id, std::initializer_list<const char*> topics);   // 推送 DATA_CHANGED
    void persist();   // 改了好友、群等内存数据后立即写回文件（消息类记录本来就是即时追加的）

    void onConnection(const TcpConnection::Ptr& connection);
    void onMessage(const TcpConnection::Ptr& connection, Buffer& buffer);
    void onClose(const TcpConnection::Ptr& connection);
    void removeSession(const TcpConnection::Ptr& connection);   // 业务线程：连接关闭后清理会话、通知好友下线
    void dispatch(Session& session, const Packet& packet);

    void handleLogin(Session& session, const nlohmann::json& request);
    void handleSendMessage(Session& session, const nlohmann::json& request);
    void handleSync(Session& session, const nlohmann::json& request);
    void handleRead(Session& session, const nlohmann::json& request);
    void handleCall(Session& session, const nlohmann::json& request);

    nlohmann::json messageJson(const Message& message) const;   // 推送和同步共用的消息格式
    std::optional<Message> latestFrom(const ServiceBase& me) const;   // me 刚保存的那条消息
    void notifyPresence(const std::string& accountKey, bool online);   // 通知在线的好友：我上线 / 下线了
    bool isOnline(const std::string& accountKey) const;
    ServiceBase* accountOf(const Session& session) const;
    void pushTo(const std::string& accountKey, MessageType type, const nlohmann::json& body);   // 对方在线才发
    void pushBytesTo(const std::string& accountKey, const std::string& bytes);   // 发送已经编码好的帧（群发时只编码一次）
    void send(const TcpConnection::Ptr& connection, MessageType type, nlohmann::json body);
    // 把编码好的数据交给连接发送 / 在连接所属的 I/O 线程里执行一个操作（比如发完后关闭）。
    // 多线程时先放进发件箱，本轮结束时按 I/O 线程分批投递；同一条连接的操作保持先后顺序
    void deliver(const TcpConnection::Ptr& connection, std::string bytes);
    void post(const TcpConnection::Ptr& connection, std::function<void()> task);
    void flushOutbox();
    void sendError(const TcpConnection::Ptr& connection, const nlohmann::json& request, ErrorCode code,
                   const std::string& message);
    void closeIdleConnections();

    EventLoop& loop_;
    Platform& platform_;
    Options options_;
    TcpServer server_;
    LoginToken tokens_;
    EventLoop::TimerId idleTimer_ = 0;
    std::unordered_map<std::uint64_t, Session> sessions_;              // 连接编号 → 会话
    std::unordered_map<std::string, std::uint64_t> online_;            // "QQ 10001" → 连接编号
    std::unordered_map<std::string, RecentMessages> recent_;           // "QQ 10001" → 最近的消息回执
    std::unordered_map<std::string, ApiHandler> api_;                  // op → 处理函数
    std::vector<std::pair<TcpConnection::Ptr, std::function<void()>>> outbox_;   // 本轮要交给 I/O 线程的操作
};
