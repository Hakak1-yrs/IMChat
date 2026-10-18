#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include "Packet.h"

class Buffer;
class EventLoop;
class TcpClient;
class TcpConnection;

// 聊天客户端的网络层：在后台线程里运行事件循环，负责连接、登录、心跳、断线重连和消息的可靠发送。
// 公开的接口都可以在任何线程调用；事件通过回调通知。回调在网络线程里执行，
// 图形界面需要自己转到界面线程（比如 PostMessage）。
//
// 可靠性：
// - 每条要发的消息先进入“待确认”队列，收到服务端回执才移除。断线期间发的消息，以及发出后还没等到回执就断线的消息，
//   重新连上并登录后按原顺序、用原来的 clientMsgId 重发；服务端按 clientMsgId 去重，所以不会丢，也不会重复。
// - 收到的消息记下最大序号，重新登录后从这个序号之后同步，离线期间的消息一条不落；
//   推送和同步可能重叠，同一条消息（同一序号）只通知一次。
// - 心跳：定期发 PING；太久没收到服务端的任何数据，就认为连接已经失效（比如网络被切断），主动断开并重连
class ChatClient {
public:
    enum class State { DISCONNECTED, CONNECTING, CONNECTED, LOGGED_IN };

    struct Callbacks {
        std::function<void(State)> onState;
        std::function<void(bool ok, const nlohmann::json& response)> onLogin;
        // 新消息，按序号去重。history 为 true 表示是上线时同步到的离线消息/历史消息（界面不必逐条提醒）
        std::function<void(const nlohmann::json& message, bool history)> onMessage;
        std::function<void(const nlohmann::json& ack)> onAck;           // 自己发的消息的回执（ok 为 false 时带 error）
        std::function<void(const nlohmann::json& receipt)> onReadReceipt;
        std::function<void(const std::string& reason)> onKicked;        // 账号在别处登录，不再自动重连
        std::function<void(const nlohmann::json& error)> onError;
        std::function<void(MessageType type, const nlohmann::json& body)> onPush;   // 其他推送：好友上下线、消息被撤回、数据变化……
    };
    // 通用请求的结果：ok 为 false 时 error 是原因（包括“连接已断开”）
    using CallDone = std::function<void(bool ok, const nlohmann::json& data, const std::string& error)>;

    struct Options {
        std::chrono::milliseconds heartbeat{ 15000 };   // 心跳间隔；3 倍时间内没有收到任何数据就重连
        std::chrono::milliseconds retryInitial{ 500 };
        std::chrono::milliseconds retryMax{ 30000 };
    };

    explicit ChatClient(Callbacks callbacks);
    ChatClient(Callbacks callbacks, Options options);
    ~ChatClient();
    ChatClient(const ChatClient&) = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    void start(const std::string& host, std::uint16_t port);   // 启动网络线程并开始连接（断开后自动重连）
    void stop();                                               // 断开并结束网络线程

    // 登录信息会被记住，断线重连后自动重新登录（登录成功后改用服务端签发的令牌，不再保存密码）
    void login(const std::string& service, const std::string& account, const std::string& password);
    // 凭令牌登录本人的另一个服务账号（令牌来自用密码登录时的 LOGIN_RESP）
    void loginWithToken(const std::string& service, const std::string& account, const std::string& token);
    std::string sendText(const std::string& to, const std::string& text);   // 私信，返回 clientMsgId
    std::string sendGroupText(int groupNo, const std::string& text);
    void markRead(const std::string& peer, int seq);
    void markGroupRead(int groupNo, int seq);
    // 通用请求（CALL_REQ），done 在网络线程里调用。未登录或连接断开时立即以失败结束
    void call(const std::string& op, nlohmann::json args, CallDone done);

    State state() const;
    std::size_t pendingCount() const;   // 等待回执的消息数
    int lastSeq() const;                // 收到的最大消息序号

private:
    struct Credentials {
        std::string service;
        std::string account;
        std::string password;
        std::string token;   // 不为空时用令牌登录
    };

    void onConnected(const std::shared_ptr<TcpConnection>& connection);
    void onDisconnected();
    void onBytes(const std::shared_ptr<TcpConnection>& connection, Buffer& buffer);
    void handle(const Packet& packet);
    void handleLoginResponse(const nlohmann::json& body);
    void handleSyncResponse(const nlohmann::json& body);
    void handleAck(const nlohmann::json& body);

    int sendPacket(MessageType type, nlohmann::json body);   // 返回本次请求的 reqId；未连接时返回 0
    void failPendingCalls(const std::string& reason);
    void sendLogin();
    void requestSync(int afterSeq);
    void flushPending();
    void heartbeat();
    void deliver(const nlohmann::json& message, bool history);
    void setState(State state);
    std::string enqueue(nlohmann::json request);
    std::string newClientMsgId();

    Callbacks callbacks_;
    Options options_;
    std::thread thread_;
    EventLoop* loop_ = nullptr;          // 由网络线程创建和销毁
    std::unique_ptr<TcpClient> client_;  // 只在网络线程里访问

    std::atomic<State> state_{ State::DISCONNECTED };
    std::atomic<int> lastSeq_{ 0 };
    std::atomic<std::size_t> pendingCount_{ 0 };

    // ---- 以下只在网络线程里访问 ----
    std::optional<Credentials> credentials_;
    std::deque<std::string> pendingOrder_;                      // 待确认消息的发送顺序
    std::unordered_map<std::string, nlohmann::json> pending_;   // clientMsgId → 请求
    std::unordered_set<int> delivered_;                         // 已经通知过的消息序号
    std::unordered_map<int, CallDone> calls_;                   // reqId → 等待结果的通用请求
    int nextReqId_ = 1;
    std::uint64_t idCounter_ = 0;
    std::uint64_t idPrefix_ = 0;
};
