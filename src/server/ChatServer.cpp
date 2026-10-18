#include "ChatServer.h"
#include <vector>
#include "Log.h"
#include "Platform.h"
#include "ServiceType.h"

namespace {

using json = nlohmann::json;

std::string field(const json& request, const char* key) {
    auto it = request.find(key);
    return it != request.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// 响应里带上请求的 reqId，客户端据此把响应和请求对应起来（同一条连接上可以连续发多个请求，不必等响应）
json reply(const json& request) {
    json body = json::object();
    auto it = request.find("reqId");
    if (it != request.end()) body["reqId"] = *it;
    return body;
}

}  // namespace

ChatServer::ChatServer(EventLoop& loop, Platform& platform, Options options)
    : loop_(loop), platform_(platform), options_(std::move(options)), server_(loop) {}

ChatServer::~ChatServer() {
    if (idleTimer_ != 0) loop_.cancel(idleTimer_);
}

bool ChatServer::start(std::string& error) {
    server_.setConnectionCallback([this](const TcpConnection::Ptr& c) { onConnection(c); });
    server_.setMessageCallback([this](const TcpConnection::Ptr& c, Buffer& b) { onMessage(c, b); });
    server_.setCloseCallback([this](const TcpConnection::Ptr& c) { onClose(c); });
    if (!server_.listen(options_.ip, options_.port, error)) return false;
    idleTimer_ = loop_.runEvery(options_.idleCheckInterval, [this] { closeIdleConnections(); });
    Log::info("IMChatServer 正在监听 " + options_.ip + ":" + std::to_string(server_.port()));
    return true;
}

std::uint16_t ChatServer::port() const { return server_.port(); }

std::size_t ChatServer::onlineCount() const { return online_.size(); }

void ChatServer::onConnection(const TcpConnection::Ptr& connection) {
    sessions_[connection->id()] = Session{ connection };
    Log::debug("新连接 #" + std::to_string(connection->id()) + " 来自 " + connection->peer());
}

void ChatServer::onClose(const TcpConnection::Ptr& connection) {
    auto it = sessions_.find(connection->id());
    if (it == sessions_.end()) return;
    const Session& session = it->second;
    if (session.loggedIn) {
        auto online = online_.find(session.accountKey);
        // 只有它仍是该账号的当前连接时才下线：被挤下线的旧连接关闭时，不能把新登录的连接也移除
        if (online != online_.end() && online->second == connection->id()) online_.erase(online);
        Log::info(session.accountKey + " 下线");
    }
    sessions_.erase(it);
}

void ChatServer::onMessage(const TcpConnection::Ptr& connection, Buffer& buffer) {
    // 一次可能收到多帧（粘包），逐帧处理；剩下不足一帧的数据留在缓冲区里等下次（半包）
    while (connection->connected()) {
        Frame frame;
        std::size_t consumed = 0;
        const Frame::DecodeResult result = Frame::decode(buffer.peek(), buffer.readableBytes(), frame, consumed);
        if (result == Frame::DecodeResult::NEED_MORE) return;
        if (result == Frame::DecodeResult::BAD_LENGTH) {   // 不符合协议：无法再找到下一帧的起点，只能断开
            Log::warn("连接 " + connection->peer() + " 发来的帧长度不合法，断开");
            buffer.retrieveAll();
            connection->forceClose();
            return;
        }
        buffer.retrieve(consumed);
        // 每帧都重新查找会话：上一帧的处理可能已经关闭了这条连接（会话随之删除）
        auto it = sessions_.find(connection->id());
        if (it == sessions_.end()) return;
        std::optional<Packet> packet = Packet::fromFrame(frame);
        if (!packet) {
            sendError(connection, json::object(), ErrorCode::BAD_REQUEST, "正文不是合法的 JSON 对象");
            continue;
        }
        dispatch(it->second, *packet);
    }
}

void ChatServer::dispatch(Session& session, const Packet& packet) {
    switch (packet.type) {
    case MessageType::PING:
        send(session.connection, MessageType::PONG, json::object());
        return;
    case MessageType::LOGIN_REQ:
        handleLogin(session, packet.body);
        return;
    case MessageType::SEND_MSG_REQ:
        if (!session.loggedIn) {
            sendError(session.connection, packet.body, ErrorCode::NOT_LOGGED_IN, "请先登录");
            return;
        }
        handleSendMessage(session, packet.body);
        return;
    default:
        sendError(session.connection, packet.body, ErrorCode::UNKNOWN_TYPE,
                  "不支持的消息类型 " + std::to_string(static_cast<int>(packet.type)));
    }
}

void ChatServer::handleLogin(Session& session, const json& request) {
    if (session.loggedIn) {
        sendError(session.connection, request, ErrorCode::ALREADY_LOGGED_IN, "这条连接已经登录了");
        return;
    }
    json response = reply(request);
    response["ok"] = false;
    ServiceType type = ServiceType::QQ;
    if (!ServiceTypeHelper::parse(field(request, "service"), type)) {
        response["error"] = "未知的服务：" + field(request, "service");
        send(session.connection, MessageType::LOGIN_RESP, response);
        return;
    }
    const std::string id = field(request, "account");
    ServiceBase* account = platform_.findAccount(type, id);
    if (!account || !account->checkPassword(field(request, "password"))) {
        // 不区分“账号不存在”和“密码错误”，避免被用来探测哪些账号存在
        response["error"] = "账号或密码错误";
        send(session.connection, MessageType::LOGIN_RESP, response);
        return;
    }
    const std::string key = ServiceTypeHelper::code(type) + " " + id;
    auto previous = online_.find(key);
    const std::uint64_t previousId = previous != online_.end() ? previous->second : 0;
    session.loggedIn = true;
    session.accountKey = key;
    online_[key] = session.connection->id();   // 先登记新连接，旧连接关闭时就不会把它移除

    int unreadPrivate = 0, unreadGroup = 0;
    platform_.unreadCount(*account, unreadPrivate, unreadGroup);
    response["ok"] = true;
    response["nickname"] = account->nickname();
    response["unreadPrivate"] = unreadPrivate;
    response["unreadGroup"] = unreadGroup;
    send(session.connection, MessageType::LOGIN_RESP, response);
    Log::info(key + " 从 " + session.connection->peer() + " 登录");

    // 与微信相同：同一账号只能在一处登录，通知旧连接后断开它
    auto old = sessions_.find(previousId);
    if (previousId != 0 && old != sessions_.end()) {
        old->second.loggedIn = false;
        TcpConnection::Ptr oldConnection = old->second.connection;
        send(oldConnection, MessageType::KICKED, json{ { "reason", "你的账号在其他地方登录" } });
        oldConnection->shutdownAfterFlush();
    }
}

void ChatServer::handleSendMessage(Session& session, const json& request) {
    ServiceBase* me = accountOf(session);
    const std::string clientMsgId = field(request, "clientMsgId");
    const std::string to = field(request, "to");
    if (!me || clientMsgId.empty() || to.empty()) {
        sendError(session.connection, request, ErrorCode::BAD_REQUEST, "缺少 clientMsgId 或 to");
        return;
    }
    RecentMessages& recent = recent_[session.accountKey];
    auto seen = recent.acks.find(clientMsgId);
    if (seen != recent.acks.end()) {   // 重发的消息：返回第一次的回执，不再保存一遍
        json ack = seen->second;
        ack.update(reply(request));
        send(session.connection, MessageType::SEND_MSG_ACK, ack);
        return;
    }

    json ack = json{ { "clientMsgId", clientMsgId } };
    std::string error;
    if (!platform_.sendPrivate(*me, to, field(request, "text"), error)) {
        ack["ok"] = false;   // 失败的不记入去重表：客户端修正后（比如对方通过了好友验证）可以重发
        ack["error"] = error;
        ack.update(reply(request));
        send(session.connection, MessageType::SEND_MSG_ACK, ack);
        return;
    }
    // 取出刚保存的这条消息（从末尾找我发的最后一条），拿到它的序号和时间
    const std::vector<Message> history = platform_.messages().conversation(me->type(), me->id(), to);
    const Message* saved = nullptr;
    for (auto it = history.rbegin(); it != history.rend() && !saved; ++it) {
        if (it->sender() == me->id()) saved = &*it;
    }
    ack["ok"] = true;
    if (saved) {
        ack["seq"] = saved->seq();
        ack["time"] = saved->time();
    }
    recent.acks[clientMsgId] = ack;
    recent.order.push_back(clientMsgId);
    if (recent.order.size() > kRecentLimit) {
        recent.acks.erase(recent.order.front());
        recent.order.pop_front();
    }
    ack.update(reply(request));
    send(session.connection, MessageType::SEND_MSG_ACK, ack);

    // 对方在线：立即推送。不在线也没关系，消息已经保存，对方上线后能看到
    auto online = online_.find(ServiceTypeHelper::code(me->type()) + " " + to);
    if (saved && online != online_.end()) {
        auto receiver = sessions_.find(online->second);
        if (receiver != sessions_.end()) {
            send(receiver->second.connection, MessageType::MSG_PUSH,
                 json{ { "seq", saved->seq() },
                       { "service", ServiceTypeHelper::code(me->type()) },
                       { "from", me->id() },
                       { "to", to },
                       { "text", saved->text() },
                       { "time", saved->time() } });
        }
    }
}

ServiceBase* ChatServer::accountOf(const Session& session) const {
    const std::size_t space = session.accountKey.find(' ');
    ServiceType type = ServiceType::QQ;
    if (space == std::string::npos || !ServiceTypeHelper::parse(session.accountKey.substr(0, space), type)) return nullptr;
    return platform_.findAccount(type, session.accountKey.substr(space + 1));
}

void ChatServer::send(const TcpConnection::Ptr& connection, MessageType type, json body) {
    Packet packet;
    packet.type = type;
    packet.body = std::move(body);
    connection->send(packet.encode());
}

void ChatServer::sendError(const TcpConnection::Ptr& connection, const json& request, ErrorCode code,
                           const std::string& message) {
    json body = reply(request);
    body["code"] = static_cast<int>(code);
    body["message"] = message;
    send(connection, MessageType::ERROR_RESP, body);
}

void ChatServer::closeIdleConnections() {
    const auto now = std::chrono::steady_clock::now();
    std::vector<TcpConnection::Ptr> idle;   // 先收集再关闭：关闭会从 sessions_ 中删除，不能边遍历边删
    for (const auto& entry : sessions_) {
        if (now - entry.second.connection->lastReceive() > options_.idleTimeout) idle.push_back(entry.second.connection);
    }
    for (const TcpConnection::Ptr& connection : idle) {
        Log::info("连接 " + connection->peer() + " 长时间没有心跳，断开");
        connection->forceClose();
    }
}
