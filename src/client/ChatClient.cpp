#include "ChatClient.h"
#include <algorithm>
#include <cstdio>
#include <future>
#include <random>
#include "EventLoop.h"
#include "Log.h"
#include "TcpClient.h"

using json = nlohmann::json;

ChatClient::ChatClient(Callbacks callbacks) : ChatClient(std::move(callbacks), Options{}) {}

ChatClient::ChatClient(Callbacks callbacks, Options options) : callbacks_(std::move(callbacks)), options_(options) {
    // clientMsgId = 随机前缀 + 递增计数：同一次运行内递增，不同次运行（重启客户端）之间也几乎不可能重复
    std::random_device device;
    idPrefix_ = (static_cast<std::uint64_t>(device()) << 32) ^ device();
}

ChatClient::~ChatClient() { stop(); }

void ChatClient::start(const std::string& host, std::uint16_t port) {
    if (thread_.joinable()) return;
    std::promise<void> ready;
    // 事件循环在网络线程里创建：这样“事件循环线程”从一开始就是它，其他线程的调用都会被投递过去
    thread_ = std::thread([this, host, port, &ready] {
        EventLoop loop;
        loop_ = &loop;
        client_ = std::make_unique<TcpClient>(loop, host, port);
        client_->setRetryDelay(options_.retryInitial, options_.retryMax);
        client_->setConnectionCallback([this](const std::shared_ptr<TcpConnection>& c) { onConnected(c); });
        client_->setDisconnectCallback([this](const std::shared_ptr<TcpConnection>&) { onDisconnected(); });
        client_->setMessageCallback([this](const std::shared_ptr<TcpConnection>& c, Buffer& b) { onBytes(c, b); });
        const EventLoop::TimerId heartbeatTimer = loop.runEvery(options_.heartbeat, [this] { heartbeat(); });
        setState(State::CONNECTING);
        client_->connect();
        ready.set_value();
        loop.loop();
        loop.cancel(heartbeatTimer);
        client_.reset();
        loop_ = nullptr;
    });
    ready.get_future().wait();
}

void ChatClient::stop() {
    if (!thread_.joinable()) return;
    loop_->runInLoop([this] {
        credentials_.reset();   // 主动退出：断开时直接进入“未连接”，而不是“正在重连”
        client_->disconnect();
        loop_->quit();
    });
    thread_.join();
    setState(State::DISCONNECTED);
}

void ChatClient::login(const std::string& service, const std::string& account, const std::string& password) {
    loop_->runInLoop([this, service, account, password] {
        credentials_ = Credentials{ service, account, password };
        if (state_ == State::CONNECTED) sendLogin();   // 还没连上的话，连上后自动登录
    });
}

std::string ChatClient::sendText(const std::string& to, const std::string& text) {
    return enqueue(json{ { "to", to }, { "text", text } });
}

std::string ChatClient::sendGroupText(int groupNo, const std::string& text) {
    return enqueue(json{ { "groupNo", groupNo }, { "text", text } });
}

void ChatClient::markRead(const std::string& peer, int seq) {
    loop_->runInLoop([this, peer, seq] { sendPacket(MessageType::READ_REQ, json{ { "peer", peer }, { "seq", seq } }); });
}

void ChatClient::call(const std::string& op, json args, CallDone done) {
    loop_->runInLoop([this, op, args = std::move(args), done = std::move(done)]() mutable {
        if (state_ != State::LOGGED_IN) {
            if (done) done(false, json(), "未连接服务器");
            return;
        }
        const int reqId = sendPacket(MessageType::CALL_REQ, json{ { "op", op }, { "args", std::move(args) } });
        if (reqId == 0) {
            if (done) done(false, json(), "未连接服务器");
            return;
        }
        calls_[reqId] = std::move(done);
    });
}

void ChatClient::markGroupRead(int groupNo, int seq) {
    loop_->runInLoop(
        [this, groupNo, seq] { sendPacket(MessageType::READ_REQ, json{ { "groupNo", groupNo }, { "seq", seq } }); });
}

ChatClient::State ChatClient::state() const { return state_; }

std::size_t ChatClient::pendingCount() const { return pendingCount_; }

int ChatClient::lastSeq() const { return lastSeq_; }

std::string ChatClient::enqueue(json request) {
    std::promise<std::string> id;
    loop_->runInLoop([this, &id, request = std::move(request)]() mutable {
        const std::string clientMsgId = newClientMsgId();
        request["clientMsgId"] = clientMsgId;
        pending_[clientMsgId] = request;
        pendingOrder_.push_back(clientMsgId);
        pendingCount_ = pending_.size();
        // 已登录就立即发；否则留在队列里，登录成功后由 flushPending 发出
        if (state_ == State::LOGGED_IN) sendPacket(MessageType::SEND_MSG_REQ, request);
        id.set_value(clientMsgId);
    });
    return id.get_future().get();
}

std::string ChatClient::newClientMsgId() {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%016llx-%llu", static_cast<unsigned long long>(idPrefix_),
                  static_cast<unsigned long long>(++idCounter_));
    return buffer;
}

void ChatClient::onConnected(const std::shared_ptr<TcpConnection>&) {
    setState(State::CONNECTED);
    if (credentials_) sendLogin();
}

void ChatClient::onDisconnected() {
    // 发出去还没收到回执的消息仍在 pending_ 里，重新登录后会重发；通用请求不重发，直接告诉调用者失败了
    failPendingCalls("连接已断开");
    setState(credentials_ ? State::CONNECTING : State::DISCONNECTED);
}

void ChatClient::failPendingCalls(const std::string& reason) {
    std::unordered_map<int, CallDone> calls;
    calls.swap(calls_);
    for (auto& entry : calls) {
        if (entry.second) entry.second(false, json(), reason);
    }
}

void ChatClient::onBytes(const std::shared_ptr<TcpConnection>& connection, Buffer& buffer) {
    while (connection->connected()) {
        Frame frame;
        std::size_t consumed = 0;
        const Frame::DecodeResult result = Frame::decode(buffer.peek(), buffer.readableBytes(), frame, consumed);
        if (result == Frame::DecodeResult::NEED_MORE) return;
        if (result == Frame::DecodeResult::BAD_LENGTH) {
            connection->forceClose();
            return;
        }
        buffer.retrieve(consumed);
        if (std::optional<Packet> packet = Packet::fromFrame(frame)) handle(*packet);
    }
}

void ChatClient::handle(const Packet& packet) {
    switch (packet.type) {
    case MessageType::PONG: return;
    case MessageType::LOGIN_RESP: handleLoginResponse(packet.body); return;
    case MessageType::SYNC_RESP: handleSyncResponse(packet.body); return;
    case MessageType::SEND_MSG_ACK: handleAck(packet.body); return;
    case MessageType::MSG_PUSH: deliver(packet.body, false); return;
    case MessageType::READ_RESP: return;
    case MessageType::READ_RECEIPT:
        if (callbacks_.onReadReceipt) callbacks_.onReadReceipt(packet.body);
        return;
    case MessageType::KICKED:
        credentials_.reset();      // 别处登录了：不再自动重连、重新登录，避免两边互相挤
        client_->disconnect();
        setState(State::DISCONNECTED);
        if (callbacks_.onKicked) callbacks_.onKicked(packet.body.value("reason", ""));
        return;
    case MessageType::CALL_RESP: {
        auto it = calls_.find(packet.body.value("reqId", 0));
        if (it == calls_.end()) return;
        CallDone done = std::move(it->second);
        calls_.erase(it);
        if (done) done(packet.body.value("ok", false), packet.body.value("data", json()), packet.body.value("error", ""));
        return;
    }
    case MessageType::PRESENCE:
    case MessageType::MSG_REVOKED:
    case MessageType::DATA_CHANGED:
        if (callbacks_.onPush) callbacks_.onPush(packet.type, packet.body);
        return;
    case MessageType::ERROR_RESP:
        if (callbacks_.onError) callbacks_.onError(packet.body);
        return;
    default: Log::debug(std::string("客户端忽略消息 ") + messageTypeName(packet.type));
    }
}

void ChatClient::handleLoginResponse(const json& body) {
    const bool ok = body.value("ok", false);
    if (ok) {
        setState(State::LOGGED_IN);
        requestSync(lastSeq_);   // 先补齐离线期间的消息，同步完再重发待确认的消息
    } else {
        credentials_.reset();    // 密码错误等：重连后不再用这组信息自动登录
    }
    if (callbacks_.onLogin) callbacks_.onLogin(ok, body);
}

void ChatClient::handleSyncResponse(const json& body) {
    int cursor = 0;
    for (const json& message : body.value("messages", json::array())) {
        deliver(message, true);
        cursor = std::max(cursor, message.value("seq", 0));
    }
    if (body.value("hasMore", false) && cursor > 0) requestSync(cursor);   // 还有：从这一批的最后一条之后接着要
    else flushPending();
}

void ChatClient::handleAck(const json& body) {
    const std::string clientMsgId = body.value("clientMsgId", "");
    auto it = pending_.find(clientMsgId);
    if (it == pending_.end()) return;   // 重发导致的重复回执
    pending_.erase(it);
    pendingOrder_.erase(std::find(pendingOrder_.begin(), pendingOrder_.end(), clientMsgId));
    pendingCount_ = pending_.size();
    // 自己发的消息之后也会出现在同步结果里：记下序号，不再当作新消息通知
    if (body.value("ok", false)) delivered_.insert(body.value("seq", 0));
    if (callbacks_.onAck) callbacks_.onAck(body);
}

int ChatClient::sendPacket(MessageType type, json body) {
    std::shared_ptr<TcpConnection> connection = client_->connection();
    if (!connection || !connection->connected()) return 0;
    const int reqId = nextReqId_++;
    body["reqId"] = reqId;
    Packet packet;
    packet.type = type;
    packet.body = std::move(body);
    connection->send(packet.encode());
    return reqId;
}

void ChatClient::sendLogin() {
    sendPacket(MessageType::LOGIN_REQ, json{ { "service", credentials_->service },
                                             { "account", credentials_->account },
                                             { "password", credentials_->password } });
}

void ChatClient::requestSync(int afterSeq) { sendPacket(MessageType::SYNC_REQ, json{ { "afterSeq", afterSeq } }); }

void ChatClient::flushPending() {
    for (const std::string& clientMsgId : pendingOrder_) sendPacket(MessageType::SEND_MSG_REQ, pending_[clientMsgId]);
}

void ChatClient::heartbeat() {
    std::shared_ptr<TcpConnection> connection = client_->connection();
    if (!connection || !connection->connected()) return;
    if (std::chrono::steady_clock::now() - connection->lastReceive() > options_.heartbeat * 3) {
        Log::info("长时间没有收到服务端的数据，重新连接");
        connection->forceClose();   // TcpClient 会自动重连，重连后重新登录、同步、重发
        return;
    }
    sendPacket(MessageType::PING, json::object());
}

void ChatClient::deliver(const json& message, bool history) {
    const int seq = message.value("seq", 0);
    if (seq <= 0 || !delivered_.insert(seq).second) return;   // 已经通知过（推送和同步重叠，或是自己发的）
    if (seq > lastSeq_) lastSeq_ = seq;
    if (callbacks_.onMessage) callbacks_.onMessage(message, history);
}

void ChatClient::setState(State state) {
    if (state_.exchange(state) != state && callbacks_.onState) callbacks_.onState(state);
}
