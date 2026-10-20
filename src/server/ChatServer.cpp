#include "ChatServer.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <optional>
#include <set>
#include <vector>
#include "Date.h"
#include "Group.h"
#include "GroupPolicy.h"
#include "GroupService.h"
#include "IndependentIdService.h"
#include "Log.h"
#include "Password.h"
#include "Platform.h"
#include "Poller.h"
#include "User.h"
#include "ServiceType.h"
#include "Util.h"
#include "WebTransport.h"

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

// 整数字段：缺少或类型不对时返回 fallback
long long number(const json& request, const char* key, long long fallback) {
    auto it = request.find(key);
    return it != request.end() && it->is_number_integer() ? it->get<long long>() : fallback;
}


json memberJson(const GroupMemberInfo& m) {
    return json{ { "id", m.id },         { "nickname", m.nickname },       { "owner", m.owner },
                 { "admin", m.admin },   { "muted", m.muted },             { "canSetAdmin", m.canSetAdmin },
                 { "canMute", m.canMute }, { "canTransfer", m.canTransfer }, { "canKick", m.canKick } };
}

json groupInfoJson(const GroupInfo& g) {
    if (!g.exists) return json{ { "exists", false }, { "groupNo", g.groupNo } };
    json members = json::array(), pending = json::array(), subGroups = json::array();
    for (const GroupMemberInfo& m : g.members) members.push_back(memberJson(m));
    for (const GroupMemberInfo& m : g.pending) pending.push_back(memberJson(m));
    for (const SubGroupInfo& s : g.subGroups) {
        subGroups.push_back({ { "name", s.name }, { "creator", s.creator }, { "members", s.members },
                              { "canAdd", s.canAdd }, { "canDissolve", s.canDissolve } });
    }
    return json{ { "exists", true },
                 { "groupNo", g.groupNo },
                 { "name", g.name },
                 { "mode", g.mode },
                 { "modeName", g.modeName },
                 { "owner", g.owner },
                 { "notice", g.notice },
                 { "preset", g.preset },
                 { "features", g.features },
                 { "members", std::move(members) },
                 { "pending", std::move(pending) },
                 { "subGroups", std::move(subGroups) },
                 { "frozenAdmins", g.frozenAdmins },
                 { "frozenMuted", g.frozenMuted },
                 { "member", g.member },
                 { "isOwner", g.isOwner },
                 { "isAdmin", g.isAdmin },
                 { "applied", g.applied },
                 { "canEditInfo", g.canEditInfo },
                 { "allowApplyJoin", g.allowApplyJoin },
                 { "allowSubGroup", g.allowSubGroup },
                 { "allowAdmin", g.allowAdmin },
                 { "allowMute", g.allowMute },
                 { "canApprove", g.canApprove },
                 { "canDissolve", g.canDissolve } };
}

}  // namespace

ChatServer::ChatServer(EventLoop& loop, Platform& platform, Options options)
    : loop_(loop),
      platform_(platform),
      options_(std::move(options)),
      server_(loop),
      tokens_(options_.tokenSecret.empty() ? Password::randomHex(32) : options_.tokenSecret) {
    registerApi();
}

ChatServer::~ChatServer() {
    if (idleTimer_ != 0) loop_.cancel(idleTimer_);
    if (statsTimer_ != 0) loop_.cancel(statsTimer_);
    server_.stop();   // 先停掉 I/O 线程：它们的回调会访问下面这些成员，必须在成员销毁之前停
    if (batchedStore_) platform_.messages().setCommitScheduler(nullptr);   // 提交剩下的写入，之后不再安排提交任务
    if (dataDirty_) saveData();   // 还没来得及保存的好友、群修改
}

bool ChatServer::start(std::string& error) {
    server_.setConnectionCallback([this](const TcpConnection::Ptr& c) { onConnection(c); });
    server_.setMessageCallback([this](const TcpConnection::Ptr& c, Buffer& b) { onMessage(c, b); });
    server_.setCloseCallback([this](const TcpConnection::Ptr& c) { onClose(c); });
    server_.setThreadCount(options_.ioThreads);
    server_.setMaxConnections(options_.maxConnections);
    // 存储支持批量提交（SQLite）时打开：本轮的写入合并成一个事务，在本轮的待办任务里提交
    batchedStore_ = platform_.messages().setCommitScheduler([this](std::function<void()> task) { loop_.queueInLoop(std::move(task)); });
    useOutbox_ = options_.ioThreads > 0 || batchedStore_;
    if (!server_.listen(options_.ip, options_.port, error)) return false;
    idleTimer_ = loop_.runEvery(options_.idleCheckInterval, [this] { closeIdleConnections(); });
    statsTimer_ = loop_.runEvery(std::chrono::minutes(1), [this] { logStats(); });
    Log::info("IMChatServer 正在监听 " + options_.ip + ":" + std::to_string(server_.port()) + "（" + Poller::defaultName() + "）" +
              (options_.ioThreads > 0 ? "，I/O 线程 " + std::to_string(options_.ioThreads) + " 个" : ""));
    return true;
}

std::uint16_t ChatServer::port() const { return server_.port(); }

std::size_t ChatServer::onlineCount() const { return online_.size(); }

// 线程：连接的三个回调在连接所属的 I/O 线程里调用。会话表、Platform 等业务状态只属于业务线程（loop_），
// 所以这里只做切帧、解析 JSON 这类与业务无关的工作，再把结果交给业务线程处理。
// 没有另开 I/O 线程时，两者是同一个线程，runInLoop 会立即执行，与单线程版本完全相同

void ChatServer::onConnection(const TcpConnection::Ptr& connection) {
    loop_.runInLoop([this, connection] {
        Session& session = sessions_[connection->id()];
        session.connection = connection;
        session.connectedAt = std::chrono::steady_clock::now();
        Log::debug("新连接 #" + std::to_string(connection->id()) + " 来自 " + connection->peer());
    });
}

void ChatServer::onClose(const TcpConnection::Ptr& connection) {
    loop_.runInLoop([this, connection] { removeSession(connection); });
}

void ChatServer::removeSession(const TcpConnection::Ptr& connection) {
    auto it = sessions_.find(connection->id());
    if (it == sessions_.end()) return;
    const std::set<int> rooms = it->second.rooms;   // 掉线也算离开群聊室，室内的人看得到
    for (int groupNo : rooms) leaveRoom(it->second, groupNo);
    const Session& session = it->second;
    const std::string accountKey = session.loggedIn ? session.accountKey : "";
    bool wentOffline = false;
    if (session.loggedIn) {
        auto online = online_.find(session.accountKey);
        // 只有它仍是该账号的当前连接时才下线：被挤下线的旧连接关闭时，不能把新登录的连接也移除
        if (online != online_.end() && online->second == connection->id()) {
            online_.erase(online);
            wentOffline = true;
            // 去重表从下线这一刻起计时：客户端重连后还会重发没收到回执的消息
            auto recent = recent_.find(session.accountKey);
            if (recent != recent_.end()) recent->second.lastUsed = std::chrono::steady_clock::now();
        }
        Log::info(session.accountKey + " 下线");
    }
    sessions_.erase(it);
    if (wentOffline) notifyPresence(accountKey, false);
}

void ChatServer::onMessage(const TcpConnection::Ptr& connection, Buffer& input) {
    // 浏览器的连接先拆掉 WebSocket 这一层，得到和原生连接一样的协议帧字节流
    Buffer* stream = WebTransport::unwrap(connection, input);
    if (!stream) return;
    Buffer& buffer = *stream;
    // 一次可能收到多帧（粘包），逐帧切出来；剩下不足一帧的数据留在缓冲区里等下次（半包）。
    // 解析失败的帧记为空，交给业务线程回复错误
    std::vector<std::optional<Packet>> packets;
    while (true) {
        Frame frame;
        std::size_t consumed = 0;
        const Frame::DecodeResult result = Frame::decode(buffer.peek(), buffer.readableBytes(), frame, consumed);
        if (result == Frame::DecodeResult::NEED_MORE) break;
        if (result == Frame::DecodeResult::BAD_LENGTH) {   // 不符合协议：无法再找到下一帧的起点，只能断开
            Log::warn("连接 " + connection->peer() + " 发来的帧长度不合法，断开");
            buffer.retrieveAll();
            connection->forceClose();
            break;
        }
        buffer.retrieve(consumed);
        packets.push_back(Packet::fromFrame(frame));
    }
    if (packets.empty()) return;
    // 一批帧一起交给业务线程：跨线程投递一次，而不是每帧一次
    loop_.runInLoop([this, connection, packets = std::move(packets)] {
        for (const std::optional<Packet>& packet : packets) {
            // 每帧都重新查找会话：上一帧的处理可能已经关闭了这条连接（会话随之删除）
            auto it = sessions_.find(connection->id());
            if (it == sessions_.end()) return;
            if (!packet) {
                sendError(connection, json::object(), ErrorCode::BAD_REQUEST, "正文不是合法的 JSON 对象");
                continue;
            }
            dispatch(it->second, *packet);
        }
    });
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
    case MessageType::SYNC_REQ:
    case MessageType::READ_REQ:
    case MessageType::CALL_REQ:
        if (!session.loggedIn) {
            // 注册新用户之类的操作本来就在登录之前
            if (packet.type == MessageType::CALL_REQ && publicApi_.count(field(packet.body, "op"))) {
                handleCall(session, packet.body);
                return;
            }
            sendError(session.connection, packet.body, ErrorCode::NOT_LOGGED_IN, "请先登录");
            return;
        }
        if (packet.type == MessageType::SEND_MSG_REQ) handleSendMessage(session, packet.body);
        else if (packet.type == MessageType::SYNC_REQ) handleSync(session, packet.body);
        else if (packet.type == MessageType::READ_REQ) handleRead(session, packet.body);
        else handleCall(session, packet.body);
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
    const std::string key = ServiceTypeHelper::code(type) + " " + id;
    // 防暴力猜密码：同一账号、同一 IP 短时间内失败太多次就暂时锁定。锁定期间直接拒绝，不再校验密码，
    // 否则攻击者仍能从“锁定时也能区分对错”中得到信息；账号不存在也照样计数，不暴露哪些账号存在
    const std::string ip = ipOf(session.connection->peer());
    const long long retryAfter = lockedFor({ "A " + key, "I " + ip });
    if (retryAfter > 0) {
        response["error"] = "登录失败次数过多，请 " + std::to_string(retryAfter) + " 秒后再试";
        response["retryAfter"] = retryAfter;
        send(session.connection, MessageType::LOGIN_RESP, response);
        return;
    }
    ServiceBase* account = platform_.findAccount(type, id);
    User* owner = platform_.ownerOf(account);
    const std::string token = field(request, "token");
    if (!token.empty()) {
        // 凭令牌登录：令牌签名正确、没有过期，并且这个账号属于令牌对应的用户
        int userId = 0;
        if (!owner || !tokens_.verify(token, userId) || owner->id() != userId) {
            recordLoginFailure("A " + key, options_.maxLoginFailures);
            recordLoginFailure("I " + ip, options_.maxLoginFailures * 4);
            response["error"] = "登录已失效，请重新输入密码登录";
            send(session.connection, MessageType::LOGIN_RESP, response);
            return;
        }
    } else if (!account || !owner || !account->checkPassword(field(request, "password"))) {
        recordLoginFailure("A " + key, options_.maxLoginFailures);
        recordLoginFailure("I " + ip, options_.maxLoginFailures * 4);   // 同一 IP 换着账号试：门槛高一些（可能是多人共用出口）
        // 不区分“账号不存在”和“密码错误”，避免被用来探测哪些账号存在
        response["error"] = "账号或密码错误";
        send(session.connection, MessageType::LOGIN_RESP, response);
        return;
    }
    throttles_.erase("A " + key);   // 登录成功：账号的失败计数清零（IP 的不清，避免猜中一个账号就洗白）
    auto previous = online_.find(key);
    const std::uint64_t previousId = previous != online_.end() ? previous->second : 0;
    session.loggedIn = true;
    session.accountKey = key;
    online_[key] = session.connection->id();   // 先登记新连接，旧连接关闭时就不会把它移除

    int unreadPrivate = 0, unreadGroup = 0;
    platform_.unreadCount(*account, unreadPrivate, unreadGroup);
    response["ok"] = true;
    response["nickname"] = account->nickname();
    response["location"] = account->location();
    response["momentsName"] = account->momentsName();
    response["unreadPrivate"] = unreadPrivate;
    response["unreadGroup"] = unreadGroup;
    // 令牌：重连时代替密码；本人开通的其他服务也凭它登录（一处登录、多个服务同时在线）
    response["token"] = tokens_.issue(owner->id());
    json services = json::array();
    for (const ServiceBase* s : owner->services()) {
        if (s == account) continue;
        services.push_back({ { "service", ServiceTypeHelper::code(s->type()) }, { "account", s->id() }, { "nickname", s->nickname() } });
    }
    response["services"] = std::move(services);
    send(session.connection, MessageType::LOGIN_RESP, response);
    ++stats_.logins;
    Log::info(key + " 从 " + session.connection->peer() + " 登录");
    if (previousId == 0) notifyPresence(key, true);   // 换一处登录不算“上线”

    // 与微信相同：同一账号只能在一处登录，通知旧连接后断开它
    auto old = sessions_.find(previousId);
    if (previousId != 0 && old != sessions_.end()) {
        old->second.loggedIn = false;
        TcpConnection::Ptr oldConnection = old->second.connection;
        send(oldConnection, MessageType::KICKED, json{ { "reason", "你的账号在其他地方登录" } });
        post(oldConnection, [oldConnection] { oldConnection->shutdownAfterFlush(); });   // 排在 KICKED 之后执行
    }
}

void ChatServer::handleSendMessage(Session& session, const json& request) {
    ServiceBase* me = accountOf(session);
    const std::string clientMsgId = field(request, "clientMsgId");
    const std::string to = field(request, "to");
    const long long groupNo = number(request, "groupNo", 0);
    if (!me || clientMsgId.empty() || (to.empty() == (groupNo == 0))) {
        sendError(session.connection, request, ErrorCode::BAD_REQUEST, "需要 clientMsgId，以及 to 或 groupNo 二者之一");
        return;
    }
    RecentMessages& recent = recent_[session.accountKey];
    recent.lastUsed = std::chrono::steady_clock::now();
    auto seen = recent.acks.find(clientMsgId);
    if (seen != recent.acks.end()) {   // 重发的消息：返回第一次的回执，不再保存一遍
        json ack = { { "clientMsgId", clientMsgId }, { "ok", true }, { "seq", seen->second.seq }, { "time", seen->second.time } };
        ack.update(reply(request));
        send(session.connection, MessageType::SEND_MSG_ACK, ack);
        return;
    }
    if (!takeToken(session)) {   // 发得太快（刷屏或客户端出错）：拒绝这一条，不保存、不推送；客户端可以稍后用同一个编号重发
        ++stats_.rateLimited;
        json ack = { { "clientMsgId", clientMsgId }, { "ok", false }, { "error", "发送太频繁，请稍后再试" }, { "retryable", true } };
        ack.update(reply(request));
        send(session.connection, MessageType::SEND_MSG_ACK, ack);
        return;
    }

    // 交给 Platform 按业务规则保存（私信只能发给好友、群消息只有成员能发、被禁言不能发……）
    const std::string service = ServiceTypeHelper::code(me->type());
    const std::string text = field(request, "text");
    std::string error;
    std::optional<Message> saved;
    std::vector<std::string> receivers;
    if (groupNo != 0) {
        Group* group = platform_.groups().find(me->type(), static_cast<int>(groupNo));
        if (!group) error = "群 " + std::to_string(groupNo) + " 不存在";
        else if (platform_.sendGroupMessage(*me, *group, text, error)) {
            saved = latestFrom(*me);
            for (const std::string& member : group->members()) {
                if (member != me->id()) receivers.push_back(member);
            }
        }
    } else if (platform_.sendPrivate(*me, to, text, error)) {
        saved = latestFrom(*me);
        receivers.push_back(to);
    }

    json ack = json{ { "clientMsgId", clientMsgId } };
    if (!saved) {
        ack["ok"] = false;   // 失败的不记入去重表：客户端修正后（比如对方通过了好友验证）可以重发
        ack["error"] = error.empty() ? "消息保存失败" : error;
        ack.update(reply(request));
        send(session.connection, MessageType::SEND_MSG_ACK, ack);
        return;
    }
    ++stats_.messages;
    ack["ok"] = true;
    ack["seq"] = saved->seq();
    ack["time"] = saved->time();
    recent.acks[clientMsgId] = SavedAck{ saved->seq(), saved->time() };
    recent.order.push_back(clientMsgId);
    if (recent.order.size() > kRecentLimit) {
        recent.acks.erase(recent.order.front());
        recent.order.pop_front();
    }
    ack.update(reply(request));
    send(session.connection, MessageType::SEND_MSG_ACK, ack);

    // 在线的接收者立即推送；不在线的上线后通过 SYNC_REQ 取到
    // 群消息要推给很多人：只编码一次，同一份字节发给每个在线的接收者，省去重复的 JSON 序列化
    // 群发时，业务线程对每个接收者只做查找和登记，帧的复制在各个 I/O 线程里完成
    Packet push;
    push.type = MessageType::MSG_PUSH;
    push.body = messageJson(*saved);
    const auto bytes = std::make_shared<const std::string>(push.encode());
    std::string key = service + " ";   // 复用同一个字符串拼接账号，避免每个接收者都分配一次
    const std::size_t prefix = key.size();
    for (const std::string& receiver : receivers) {
        key.resize(prefix);
        key += receiver;
        pushBytesTo(key, bytes);
    }
}

std::string ChatServer::ipOf(const std::string& peer) {
    const std::size_t colon = peer.rfind(':');
    return colon == std::string::npos ? peer : peer.substr(0, colon);
}

long long ChatServer::lockedFor(std::initializer_list<std::string> keys) const {
    const auto now = std::chrono::steady_clock::now();
    long long longest = 0;
    for (const std::string& key : keys) {
        auto it = throttles_.find(key);
        if (it == throttles_.end() || it->second.lockedUntil <= now) continue;
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(it->second.lockedUntil - now).count();
        longest = (std::max)(longest, (ms + 999) / 1000);   // 向上取整到秒
    }
    return longest;
}

void ChatServer::recordLoginFailure(const std::string& key, int limit) {
    if (limit <= 0) return;   // 0 表示不限制
    const auto now = std::chrono::steady_clock::now();
    LoginThrottle& t = throttles_[key];
    if (now - t.windowStart > options_.loginWindow) {   // 统计窗口过去了：重新计数
        t.windowStart = now;
        t.failures = 0;
    }
    if (++t.failures < limit) return;
    t.lockedUntil = now + options_.loginLockout;
    t.failures = 0;
    t.windowStart = now;
    ++stats_.loginLockouts;
    Log::warn("登录失败次数过多，锁定 " + key);
}

bool ChatServer::takeToken(Session& session) {
    // 令牌桶：桶里最多 messageBurst 个令牌，每秒补充 messageRate 个，每发一条消息用掉一个。
    // 允许短时间的突发（比如断线重连后补发积压的消息），但长期速率不能超过 messageRate
    if (options_.messageRate <= 0) return true;
    const auto now = std::chrono::steady_clock::now();
    const double burst = options_.messageBurst > 0 ? options_.messageBurst : options_.messageRate;
    if (session.bucketTime == std::chrono::steady_clock::time_point{}) session.tokens = burst;
    else session.tokens += std::chrono::duration<double>(now - session.bucketTime).count() * options_.messageRate;
    session.tokens = (std::min)(session.tokens, burst);
    session.bucketTime = now;
    if (session.tokens < 1.0) return false;
    session.tokens -= 1.0;
    return true;
}

json ChatServer::statsJson() const {
    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime_).count();
    return json{ { "uptimeSeconds", uptime },
                 { "ioThreads", options_.ioThreads },
                 { "connections", sessions_.size() },
                 { "rejectedConnections", server_.rejectedCount() },
                 { "online", online_.size() },
                 { "logins", stats_.logins },
                 { "messages", stats_.messages },
                 { "pushes", stats_.pushes },
                 { "calls", stats_.calls },
                 { "rateLimited", stats_.rateLimited },
                 { "loginLockouts", stats_.loginLockouts },
                 { "dataSaves", stats_.dataSaves },
                 { "dedupAccounts", recent_.size() } };
}

void ChatServer::logStats() {
    // 每分钟一行运行概况（这一分钟没有新消息时不打印）
    const long long messages = stats_.messages - lastLoggedMessages_;
    lastLoggedMessages_ = stats_.messages;
    if (messages == 0) return;
    Log::info("运行概况：连接 " + std::to_string(sessions_.size()) + "，在线 " + std::to_string(online_.size()) + "，近一分钟消息 " +
              std::to_string(messages) + " 条，累计推送 " + std::to_string(stats_.pushes) + " 条，因发送过快被拒 " +
              std::to_string(stats_.rateLimited) + " 条");
}

std::optional<Message> ChatServer::latestFrom(const ServiceBase& me) const {
    // 刚保存的那条消息（拿到它的序号和时间）。所有写入都在事件循环线程里，取到的一定是它
    Message message(0, me.type(), false, "", "", "", "");
    if (!platform_.messages().lastFrom(me.type(), me.id(), message)) return std::nullopt;
    return message;
}

void ChatServer::handleSync(Session& session, const json& request) {
    ServiceBase* me = accountOf(session);
    if (!me) return;
    const long long afterSeq = number(request, "afterSeq", 0);
    long long limit = number(request, "limit", kSyncDefaultLimit);
    if (limit <= 0 || limit > kSyncMaxLimit) limit = kSyncMaxLimit;

    std::vector<int> groupNos;
    for (const Group* group : platform_.groups().groupsOf(me->type(), me->id())) groupNos.push_back(group->groupNo());
    // 只取 afterSeq 之后的 limit + 1 条：多出来的那一条说明还有，剩下的让客户端接着要
    std::vector<Message> found = platform_.messages().messagesAfter(
        me->type(), me->id(), groupNos, static_cast<int>(std::clamp<long long>(afterSeq, 0, INT_MAX)),
        static_cast<std::size_t>(limit) + 1);
    const bool hasMore = found.size() > static_cast<std::size_t>(limit);
    if (hasMore) found.pop_back();
    json messages = json::array();
    for (const Message& m : found) messages.push_back(messageJson(m));
    json response = reply(request);
    response["messages"] = std::move(messages);
    response["hasMore"] = hasMore;
    send(session.connection, MessageType::SYNC_RESP, response);
}

void ChatServer::handleRead(Session& session, const json& request) {
    ServiceBase* me = accountOf(session);
    const std::string peer = field(request, "peer");
    const long long groupNo = number(request, "groupNo", 0);
    const long long seq = number(request, "seq", 0);
    if (!me || seq <= 0 || (peer.empty() == (groupNo == 0))) {
        sendError(session.connection, request, ErrorCode::BAD_REQUEST, "需要 seq，以及 peer 或 groupNo 二者之一");
        return;
    }
    const std::string service = ServiceTypeHelper::code(me->type());
    if (groupNo != 0) {
        // 群消息只记录已读位置，不发回执：群里人多，每人每读一次都通知所有人代价太大
        platform_.messages().markGroupRead(me->type(), static_cast<int>(groupNo), me->id(), static_cast<int>(seq));
    } else {
        platform_.messages().markPrivateRead(me->type(), me->id(), peer, static_cast<int>(seq));
        pushTo(service + " " + peer, MessageType::READ_RECEIPT, json{ { "reader", me->id() }, { "seq", seq } });
    }
    json response = reply(request);
    response["ok"] = true;
    send(session.connection, MessageType::READ_RESP, response);
}

void ChatServer::pushTo(const std::string& accountKey, MessageType type, const json& body) {
    auto online = online_.find(accountKey);
    if (online == online_.end()) return;
    auto receiver = sessions_.find(online->second);
    if (receiver == sessions_.end()) return;
    if (type == MessageType::MSG_PUSH) ++stats_.pushes;
    send(receiver->second.connection, type, body);
}

void ChatServer::pushBytesTo(const std::string& accountKey, const std::shared_ptr<const std::string>& bytes) {
    auto online = online_.find(accountKey);
    if (online == online_.end()) return;
    auto receiver = sessions_.find(online->second);
    if (receiver == sessions_.end()) return;
    ++stats_.pushes;
    deliverShared(receiver->second.connection, bytes);
}

// ======================== 通用请求（CALL_REQ） ========================

void ChatServer::registerApi() {
    api_["contacts.list"] = [this](Session&, ServiceBase& me, const json&) { return contactsList(me); };
    api_["groups.list"] = [this](Session&, ServiceBase& me, const json&) { return groupsList(me); };
    api_["reads.get"] = [this](Session&, ServiceBase& me, const json&) { return readsGet(me); };
    api_["message.revoke"] = [this](Session&, ServiceBase& me, const json& args) { return revokeMessage(me, args); };
    api_["social.get"] = [this](Session&, ServiceBase& me, const json&) { return socialGet(me); };
    api_["profile.get"] = [this](Session&, ServiceBase& me, const json& args) { return profileGet(me, args); };
    api_["friends.request"] = [this](Session&, ServiceBase& me, const json& args) { return requestFriend(me, args); };
    api_["friends.respond"] = [this](Session&, ServiceBase& me, const json& args) { return respondRequest(me, args); };
    api_["friends.remove"] = [this](Session&, ServiceBase& me, const json& args) { return removeFriend(me, args); };
    api_["friends.remark"] = [this](Session&, ServiceBase& me, const json& args) { return setRemark(me, args); };
    api_["friends.block"] = [this](Session&, ServiceBase& me, const json& args) { return setBlocked(me, args); };
    api_["settings.verify"] = [this](Session&, ServiceBase& me, const json& args) { return setVerification(me, args); };
    api_["groups.all"] = [this](Session&, ServiceBase& me, const json&) { return groupsAll(me); };
    api_["group.get"] = [this](Session&, ServiceBase& me, const json& args) {
        return groupInfoJson(GroupService::describe(platform_, me, static_cast<int>(number(args, "groupNo", 0))));
    };
    api_["group.create"] = [this](Session&, ServiceBase& me, const json& args) { return createGroup(me, args); };
    api_["group.exec"] = [this](Session&, ServiceBase& me, const json& args) { return execGroup(me, args); };
    api_["server.stats"] = [this](Session&, ServiceBase&, const json&) { return statsJson(); };
    api_["moments.list"] = [this](Session&, ServiceBase& me, const json& args) { return momentsList(me, args); };
    api_["moments.notices"] = [this](Session&, ServiceBase& me, const json&) {
        return json{ { "all", platform_.momentNotices(me, false) }, { "unread", platform_.momentNotices(me, true) } };
    };
    api_["moments.markRead"] = [this](Session&, ServiceBase& me, const json&) {
        platform_.markMomentsRead(me);
        return json::object();
    };
    api_["moments.post"] = [this](Session&, ServiceBase& me, const json& args) {
        std::string msg;
        if (!platform_.postMoment(me, field(args, "text"), field(args, "scope"), msg)) throw ApiError(msg);
        notifyMomentAudience(me, me.id());
        return json{ { "message", msg } };
    };
    api_["moments.like"] = [this](Session&, ServiceBase& me, const json& args) {
        return changeMoment(me, static_cast<int>(number(args, "seq", 0)), [&](std::string& msg) {
            return platform_.toggleLike(me, static_cast<int>(number(args, "seq", 0)), msg);
        });
    };
    api_["moments.comment"] = [this](Session&, ServiceBase& me, const json& args) {
        const int seq = static_cast<int>(number(args, "seq", 0));
        return changeMoment(me, seq, [&](std::string& msg) {
            return platform_.commentMoment(me, seq, field(args, "replyTo"), field(args, "text"), msg);
        });
    };
    api_["moments.delete"] = [this](Session&, ServiceBase& me, const json& args) {
        const int seq = static_cast<int>(number(args, "seq", 0));
        return changeMoment(me, seq, [&](std::string& msg) { return platform_.deleteMoment(me, seq, msg); });
    };
    api_["moments.deleteComment"] = [this](Session&, ServiceBase& me, const json& args) {
        std::string msg;
        if (!platform_.deleteComment(me, static_cast<int>(number(args, "seq", 0)), msg)) throw ApiError(msg);
        notifyMomentAudience(me, me.id());   // 评论可能在别人的动态下：通知我的好友（含动态作者）
        return json{ { "message", msg } };
    };

    // 个人中心（单机版“用户菜单 → 个人信息”的各项操作）
    publicApi_["account.register"] = [this](Session& session, const json& args) { return registerUser(session, args); };
    api_["account.get"] = [this](Session&, ServiceBase& me, const json&) { return accountGet(me); };
    api_["account.update"] = [this](Session&, ServiceBase& me, const json& args) { return accountUpdate(me, args); };
    api_["account.password"] = [this](Session&, ServiceBase& me, const json& args) { return changePassword(me, args); };
    api_["account.openService"] = [this](Session&, ServiceBase& me, const json& args) { return openService(me, args); };
    api_["account.bindQQ"] = [this](Session&, ServiceBase& me, const json& args) { return bindQQ(me, args); };
    api_["account.closeService"] = [this](Session& session, ServiceBase& me, const json& args) { return closeService(session, me, args); };
    api_["account.deregister"] = [this](Session& session, ServiceBase& me, const json& args) { return deregister(session, me, args); };
    api_["platform.overview"] = [this](Session&, ServiceBase&, const json&) { return overview(); };
    api_["friends.cross"] = [this](Session&, ServiceBase& me, const json& args) { return crossFriends(me, args); };
    // 点对点聊天：服务端只转发双方交换连接信息的信令，聊天内容不经过服务端
    api_["p2p.signal"] = [this](Session& session, ServiceBase& me, const json& args) { return p2pSignal(session, me, args); };
    // 实时群聊室：在线的群成员进来即时聊天，内容不保存（与单机版的群聊室相同），由服务端主持
    api_["room.join"] = [this](Session& session, ServiceBase& me, const json& args) { return roomJoin(session, me, args); };
    api_["room.say"] = [this](Session& session, ServiceBase& me, const json& args) { return roomSay(session, me, args); };
    api_["room.leave"] = [this](Session& session, ServiceBase& me, const json& args) { return roomLeave(session, me, args); };
}

// ---- 个人中心 ----

namespace {

// 与单机版的输入检查相同：昵称、所在地不能为空（空格换成下划线），出生日期合法且不晚于今天，密码至少 6 位
void readProfile(const json& args, std::string& nickname, Date& birthday, std::string& location, std::string& password) {
    nickname = Util::toField(Util::lineText(field(args, "nickname")));
    location = Util::toField(Util::lineText(field(args, "location")));
    password = Util::toField(field(args, "password"));
    if (nickname.empty() || location.empty()) throw std::invalid_argument("昵称和所在地不能为空。");
    if (!Date::parse(field(args, "birthday"), birthday) || birthday.toString() > Date::today().toString())
        throw std::invalid_argument("出生日期不正确（格式为 yyyy-mm-dd，且不能晚于今天）。");
    if (password.size() < 6) throw std::invalid_argument("密码太短，请至少输入 6 位。");
}

}  // namespace

json ChatServer::registerUser(Session& session, const json& args) {
    if (!takeToken(session)) throw ApiError("操作太频繁，请稍后再试");   // 不登录就能调用：限制速率，防止被用来批量注册
    const std::string name = Util::toField(Util::lineText(field(args, "name")));
    ServiceType type = ServiceType::QQ;
    if (name.empty()) throw ApiError("真实姓名不能为空。");
    if (!ServiceTypeHelper::parse(field(args, "service"), type)) throw ApiError("未知的服务：" + field(args, "service"));
    std::string msg, nickname, location, password;
    Date birthday;
    if (!platform_.canRegisterWith(type, msg)) throw ApiError(msg);   // 例如微博与 QQ 共享号码，不能作为第一个服务
    try {
        readProfile(args, nickname, birthday, location, password);
    } catch (const std::invalid_argument& e) {
        throw ApiError(e.what());
    }
    ServiceBase* account = platform_.registerUser(name, type, nickname, birthday, location, password, msg);
    if (!account) throw ApiError(msg);
    persist();
    Log::info("新用户注册：" + ServiceTypeHelper::code(type) + " " + account->id());
    return json{ { "service", ServiceTypeHelper::code(type) }, { "account", account->id() }, { "message", msg } };
}

json ChatServer::accountGet(ServiceBase& me) {
    User* owner = platform_.ownerOf(&me);
    if (!owner) throw ApiError("账号不存在");
    json services = json::array(), available = json::array();
    for (const ServiceBase* s : owner->services()) {
        services.push_back({ { "service", ServiceTypeHelper::code(s->type()) }, { "account", s->id() }, { "nickname", s->nickname() } });
    }
    for (ServiceType t : ServiceTypeHelper::all()) {
        if (!owner->hasService(t)) available.push_back(ServiceTypeHelper::code(t));
    }
    const auto* independent = dynamic_cast<const IndependentIdService*>(&me);
    return json{ { "userId", owner->id() },
                 { "name", owner->name() },
                 { "service", ServiceTypeHelper::code(me.type()) },
                 { "serviceName", me.serviceName() },
                 { "id", me.id() },
                 { "idLabel", me.idLabel() },
                 { "nickname", me.nickname() },
                 { "location", me.location() },
                 { "birthday", me.birthday().toString() },
                 { "registerDate", me.registerDate().toString() },
                 { "tAge", me.tAge() },
                 { "info", Util::captureLines([&] { me.showInfo(); }) },   // 各服务按自己的类型输出资料（多态）
                 { "independent", independent != nullptr },   // 独立号码（微信、微商）：可以绑定 QQ
                 { "boundQQ", independent ? independent->boundQQ() : "" },
                 { "hasQQ", owner->hasService(ServiceType::QQ) },
                 { "services", std::move(services) },
                 { "available", std::move(available) } };
}

json ChatServer::accountUpdate(ServiceBase& me, const json& args) {
    // 只改传了的字段
    std::string changed;
    if (args.contains("nickname")) {
        const std::string nickname = Util::toField(Util::lineText(field(args, "nickname")));
        if (nickname.empty()) throw ApiError("昵称不能为空。");
        me.setNickname(nickname);
        changed = "昵称";
    }
    if (args.contains("location")) {
        const std::string location = Util::toField(Util::lineText(field(args, "location")));
        if (location.empty()) throw ApiError("所在地不能为空。");
        me.setLocation(location);
        changed += changed.empty() ? "所在地" : "、所在地";
    }
    if (args.contains("birthday")) {
        Date birthday;
        if (!Date::parse(field(args, "birthday"), birthday) || birthday.toString() > Date::today().toString())
            throw ApiError("日期不正确（格式为 yyyy-mm-dd，且不能晚于今天）。");
        me.setBirthday(birthday);
        changed += changed.empty() ? "出生日期" : "、出生日期";
    }
    if (changed.empty()) throw ApiError("没有要修改的内容");
    persist();
    for (const Friend& f : me.friends().all()) notifyChanged(me.type(), f.id(), { "contacts" });   // 好友那边的昵称、所在地跟着变
    return json{ { "message", changed + "已修改" } };
}

json ChatServer::changePassword(ServiceBase& me, const json& args) {
    if (!me.checkPassword(Util::toField(field(args, "old")))) throw ApiError("原密码错误。");
    const std::string next = Util::toField(field(args, "new"));
    if (next.size() < 6) throw ApiError("密码太短，请至少输入 6 位。");
    me.setPassword(next);   // 各服务账号的密码相互独立；只保存加密后的记录
    persist();
    return json{ { "message", "密码已修改" } };
}

json ChatServer::openService(ServiceBase& me, const json& args) {
    User* owner = platform_.ownerOf(&me);
    ServiceType type = ServiceType::QQ;
    if (!owner) throw ApiError("账号不存在");
    if (!ServiceTypeHelper::parse(field(args, "service"), type)) throw ApiError("未知的服务：" + field(args, "service"));
    std::string msg, nickname, location, password;
    Date birthday;
    try {
        readProfile(args, nickname, birthday, location, password);
    } catch (const std::invalid_argument& e) {
        throw ApiError(e.what());
    }
    ServiceBase* account = platform_.openService(*owner, type, nickname, birthday, location, password, msg);
    if (!account) throw ApiError(msg);
    // 微信、微商等独立号码的服务：绑定本人的 QQ 号后才能与 QQ、微博互认好友
    auto* independent = dynamic_cast<IndependentIdService*>(account);
    if (independent && owner->hasService(ServiceType::QQ) && args.value("bindQQ", true)) {
        std::string bindMsg;
        platform_.bindQQ(*owner, *independent, bindMsg);
        msg += "\n" + bindMsg;
    }
    persist();
    return json{ { "service", ServiceTypeHelper::code(type) }, { "account", account->id() }, { "message", msg } };
}

json ChatServer::bindQQ(ServiceBase& me, const json& args) {
    auto* account = dynamic_cast<IndependentIdService*>(&me);
    if (!account) throw ApiError(me.serviceName() + "与 QQ 共享号码，本来就对应你的 QQ 号，不需要绑定。");
    User* owner = platform_.ownerOf(&me);
    std::string msg;
    const bool ok = args.value("on", true) ? owner && platform_.bindQQ(*owner, *account, msg) : platform_.unbindQQ(*account, msg);
    if (!ok) throw ApiError(msg.empty() ? "操作失败" : msg);
    persist();
    return json{ { "message", msg }, { "boundQQ", account->boundQQ() } };
}

void ChatServer::beforeAccountRemoved(const ServiceBase& account, const std::string& reason, std::uint64_t except) {
    // 好友和群成员的列表里将少一个人：通知他们重新获取
    for (const Friend& f : account.friends().all()) notifyChanged(account.type(), f.id(), { "contacts", "social" });
    for (const Group* g : platform_.groups().groupsOf(account.type(), account.id())) {
        for (const std::string& id : g->members()) {
            if (id != account.id()) notifyChanged(account.type(), id, { "groups" });
        }
    }
    const std::string key = ServiceTypeHelper::code(account.type()) + " " + account.id();
    auto online = online_.find(key);
    if (online == online_.end()) return;
    auto session = sessions_.find(online->second);
    online_.erase(online);
    if (session == sessions_.end() || session->first == except) return;
    const std::set<int> rooms = session->second.rooms;
    for (int groupNo : rooms) leaveRoom(session->second, groupNo);
    session->second.loggedIn = false;
    TcpConnection::Ptr connection = session->second.connection;
    send(connection, MessageType::KICKED, json{ { "reason", reason } });
    post(connection, [connection] { connection->shutdownAfterFlush(); });
}

json ChatServer::closeService(Session& session, ServiceBase& me, const json& args) {
    User* owner = platform_.ownerOf(&me);
    if (!owner) throw ApiError("账号不存在");
    if (!me.checkPassword(Util::toField(field(args, "password")))) throw ApiError("密码错误。");
    if (owner->services().size() == 1) throw ApiError("这是你唯一开通的服务，关闭后将无法登录。如需离开平台，请选择“注销整个账号”。");
    const std::set<int> rooms = session.rooms;
    for (int groupNo : rooms) leaveRoom(session, groupNo);
    beforeAccountRemoved(me, "", session.connection->id());
    const std::string closed = me.serviceName();
    std::string msg;
    if (!platform_.closeService(*owner, me.type(), msg)) throw ApiError(msg);   // 之后 me 已被删除，不能再访问
    persist();
    // 这条连接回到未登录状态：客户端凭令牌改登录本人的其他服务
    session.loggedIn = false;
    session.accountKey.clear();
    json services = json::array();
    for (const ServiceBase* s : owner->services())
        services.push_back({ { "service", ServiceTypeHelper::code(s->type()) }, { "account", s->id() }, { "nickname", s->nickname() } });
    Log::info("用户 " + std::to_string(owner->id()) + " 关闭了" + closed);
    return json{ { "message", msg }, { "services", std::move(services) } };
}

json ChatServer::deregister(Session& session, ServiceBase& me, const json& args) {
    User* owner = platform_.ownerOf(&me);
    if (!owner) throw ApiError("账号不存在");
    if (!me.checkPassword(Util::toField(field(args, "password")))) throw ApiError("密码错误。");
    const std::set<int> rooms = session.rooms;
    for (int groupNo : rooms) leaveRoom(session, groupNo);
    for (const ServiceBase* account : owner->services()) beforeAccountRemoved(*account, "账号已注销", session.connection->id());
    const int userId = owner->id();
    std::string msg;
    platform_.deleteUser(userId, msg);   // 之后 me、owner 都已被删除
    persist();
    session.loggedIn = false;
    session.accountKey.clear();
    Log::info("用户 " + std::to_string(userId) + " 注销了账号");
    return json{ { "message", msg } };
}

json ChatServer::overview() {
    // 平台概况（单机版主菜单的同名功能）：所有用户及其开通的服务、各服务的群数量
    json users = json::array();
    for (const auto& [id, user] : platform_.users()) {
        json services = json::array();
        for (const ServiceBase* s : user.services())
            services.push_back({ { "service", ServiceTypeHelper::code(s->type()) }, { "account", s->id() }, { "nickname", s->nickname() } });
        users.push_back({ { "id", id }, { "name", user.name() }, { "services", std::move(services) } });
    }
    json groups = json::object();
    for (ServiceType t : ServiceTypeHelper::all())
        groups[ServiceTypeHelper::code(t)] = platform_.groups().groupsOfService(t).size();
    return json{ { "users", std::move(users) }, { "groups", std::move(groups) } };
}

json ChatServer::crossFriends(ServiceBase& me, const json& args) {
    // 跨服务好友（单机版“好友管理 → 推荐好友 / 共同好友”）：依据本人在另一个服务中的好友
    User* owner = platform_.ownerOf(&me);
    if (!owner) throw ApiError("账号不存在");
    json sources = json::array();
    for (const ServiceBase* s : owner->services()) {
        if (s != &me) sources.push_back(ServiceTypeHelper::code(s->type()));
    }
    ServiceType source = ServiceType::QQ;
    if (!ServiceTypeHelper::parse(field(args, "source"), source) || source == me.type() || !owner->hasService(source))
        return json{ { "sources", std::move(sources) } };   // 只列出可以选的服务
    json result = { { "sources", sources }, { "source", ServiceTypeHelper::code(source) } };

    std::vector<ServiceBase*> recommended;
    std::vector<std::string> skipped;
    std::string reason;
    json recommend = json::array();
    if (!platform_.recommendFriends(*owner, me.type(), source, recommended, skipped, reason)) {
        result["recommendError"] = reason;
    } else {
        for (ServiceBase* account : recommended) {
            const ServiceBase* there = platform_.ownerOf(account)->service(source);
            recommend.push_back({ { "id", account->id() }, { "nickname", account->nickname() },
                                  { "sourceId", there ? there->id() : "" }, { "sourceNickname", there ? there->nickname() : "" } });
        }
        result["recommendSkipped"] = skipped;
    }
    result["recommend"] = std::move(recommend);

    std::vector<User*> common;
    skipped.clear();
    json commonList = json::array();
    if (!platform_.commonFriends(*owner, me.type(), source, common, skipped, reason)) {
        result["commonError"] = reason;
    } else {
        for (User* u : common) {
            const ServiceBase* here = u->service(me.type());
            const ServiceBase* there = u->service(source);
            commonList.push_back({ { "name", u->name() }, { "id", here ? here->id() : "" }, { "nickname", here ? here->nickname() : "" },
                                   { "sourceId", there ? there->id() : "" }, { "sourceNickname", there ? there->nickname() : "" } });
        }
        result["commonSkipped"] = skipped;
    }
    result["common"] = std::move(commonList);
    return result;
}

// ---- 点对点聊天的信令、实时群聊室 ----

json ChatServer::p2pSignal(Session& session, ServiceBase& me, const json& args) {
    // 点对点聊天的消息由双方直接传送，服务端只帮它们找到对方：转发邀请、应答、连接地址这类信令。
    // 直连不通时客户端也用它中转聊天内容（不保存）。只能发给好友，内容原样转发
    const std::string to = field(args, "to");
    const auto data = args.find("data");
    if (data == args.end() || !data->is_object()) throw ApiError("缺少 data");
    if (!me.friends().contains(to)) throw ApiError(to + " 不是你的好友");
    if (data->dump().size() > 16 * 1024) throw ApiError("信令太长");
    if (!takeToken(session)) throw ApiError("发送太频繁，请稍后再试");
    const std::string key = ServiceTypeHelper::code(me.type()) + " " + to;
    if (!isOnline(key)) throw ApiError("对方不在线");
    pushTo(key, MessageType::P2P_SIGNAL, json{ { "from", me.id() }, { "nickname", me.nickname() }, { "data", *data } });
    return json::object();
}

json ChatServer::roomJoin(Session& session, ServiceBase& me, const json& args) {
    const int groupNo = static_cast<int>(number(args, "groupNo", 0));
    const Group* g = platform_.groups().find(me.type(), groupNo);
    if (!g) throw ApiError("群 " + std::to_string(groupNo) + " 不存在");
    if (!g->isMember(me.id())) throw ApiError("只有群成员可以进入群聊室");
    const std::string roomKey = ServiceTypeHelper::code(me.type()) + " " + std::to_string(groupNo);
    auto& room = rooms_[roomKey];
    const bool already = room.count(me.id()) > 0;
    room[me.id()] = session.connection->id();
    session.rooms.insert(groupNo);
    if (!already) {
        pushRoom(roomKey, json{ { "groupNo", groupNo }, { "event", "join" }, { "from", me.id() }, { "nickname", me.nickname() },
                                { "time", Date::nowText() } },
                 me.id());
    }
    json members = json::array();
    for (const auto& member : room) {
        ServiceBase* account = platform_.findAccount(me.type(), member.first);
        members.push_back({ { "id", member.first }, { "nickname", account ? account->nickname() : member.first } });
    }
    return json{ { "groupNo", groupNo }, { "name", g->name() }, { "members", std::move(members) } };
}

json ChatServer::roomSay(Session& session, ServiceBase& me, const json& args) {
    const int groupNo = static_cast<int>(number(args, "groupNo", 0));
    if (!session.rooms.count(groupNo)) throw ApiError("你不在这个群聊室里");
    const std::string text = Util::lineText(field(args, "text"));
    if (text.empty()) throw ApiError("消息不能为空");
    if (Util::charCount(text) > 1000) throw ApiError("消息太长（最多 1000 字）");
    if (!takeToken(session)) throw ApiError("发送太频繁，请稍后再试");
    const std::string time = Date::nowText();
    pushRoom(ServiceTypeHelper::code(me.type()) + " " + std::to_string(groupNo),
             json{ { "groupNo", groupNo }, { "event", "say" }, { "from", me.id() }, { "nickname", me.nickname() }, { "text", text },
                   { "time", time } },
             me.id());
    return json{ { "time", time } };
}

json ChatServer::roomLeave(Session& session, ServiceBase&, const json& args) {
    leaveRoom(session, static_cast<int>(number(args, "groupNo", 0)));
    return json::object();
}

void ChatServer::leaveRoom(Session& session, int groupNo) {
    session.rooms.erase(groupNo);
    const std::size_t space = session.accountKey.find(' ');
    if (space == std::string::npos) return;
    const std::string id = session.accountKey.substr(space + 1);
    const std::string roomKey = session.accountKey.substr(0, space) + " " + std::to_string(groupNo);
    auto room = rooms_.find(roomKey);
    if (room == rooms_.end()) return;
    auto member = room->second.find(id);
    if (member == room->second.end() || member->second != session.connection->id()) return;   // 已在别处重新进入
    room->second.erase(member);
    if (room->second.empty()) {
        rooms_.erase(room);
        return;
    }
    ServiceType type = ServiceType::QQ;
    ServiceTypeHelper::parse(session.accountKey.substr(0, space), type);
    const ServiceBase* account = platform_.findAccount(type, id);
    pushRoom(roomKey, json{ { "groupNo", groupNo }, { "event", "leave" }, { "from", id }, { "nickname", account ? account->nickname() : id },
                            { "time", Date::nowText() } },
             id);
}

void ChatServer::pushRoom(const std::string& roomKey, const json& event, const std::string& except) {
    auto room = rooms_.find(roomKey);
    if (room == rooms_.end()) return;
    for (const auto& [id, connectionId] : room->second) {
        if (id == except) continue;
        auto receiver = sessions_.find(connectionId);
        if (receiver != sessions_.end()) send(receiver->second.connection, MessageType::ROOM_EVENT, event);
    }
}

// ---- 朋友圈 ----

json ChatServer::momentsList(ServiceBase& me, const json& args) {
    json moments = json::array(), names = json::object();
    auto remember = [&](const std::string& id) {
        if (names.contains(id)) return;
        ServiceBase* account = platform_.findAccount(me.type(), id);
        names[id] = account ? account->nickname() : id;
    };
    // 可见范围、只显示共同好友的点赞和评论，这些规则都在 Platform::moments 里按查看者处理好了
    for (const Moment& m : platform_.moments(me, field(args, "author"))) {
        json comments = json::array();
        for (const MomentComment& c : m.comments) {
            comments.push_back({ { "seq", c.seq }, { "author", c.author }, { "replyTo", c.replyTo }, { "time", c.time }, { "text", c.text } });
            remember(c.author);
            if (!c.replyTo.empty()) remember(c.replyTo);
        }
        for (const std::string& id : m.likes) remember(id);
        remember(m.author);
        moments.push_back({ { "seq", m.seq },     { "author", m.author }, { "scope", m.author == me.id() ? m.scope : "" },
                            { "time", m.time },   { "text", m.text },     { "likes", m.likes },
                            { "comments", std::move(comments) } });
    }
    return json{ { "moments", std::move(moments) }, { "names", std::move(names) } };
}

json ChatServer::changeMoment(ServiceBase& me, int seq, const std::function<bool(std::string&)>& change) {
    std::string author;   // 先找到动态作者（删除之后就找不到了）
    for (const Moment& m : platform_.moments(me)) {
        if (m.seq == seq) author = m.author;
    }
    std::string msg;
    if (!change(msg)) throw ApiError(msg);
    if (!author.empty()) notifyMomentAudience(me, author);
    return json{ { "message", msg } };
}

void ChatServer::notifyMomentAudience(ServiceBase& me, const std::string& author) {
    // 能看到这条动态的只可能是作者和作者的好友：通知他们重新获取（具体能看到什么，取的时候再按规则过滤）
    ServiceBase* owner = platform_.findAccount(me.type(), author);
    if (!owner) return;
    if (author != me.id()) notifyChanged(me.type(), author, { "moments" });
    for (const Friend& f : owner->friends().all()) {
        if (f.id() != me.id()) notifyChanged(me.type(), f.id(), { "moments" });
    }
}

// ---- 群：全部群、群详情、新建、群管理命令 ----

json ChatServer::groupsAll(ServiceBase& me) {
    json groups = json::array();
    for (const GroupBrief& g : GroupService::list(platform_, me)) {
        groups.push_back({ { "groupNo", g.groupNo }, { "name", g.name }, { "modeName", g.modeName }, { "owner", g.owner },
                           { "ownerName", g.ownerName }, { "memberCount", g.memberCount }, { "joined", g.joined },
                           { "applied", g.applied }, { "allowApplyJoin", g.allowApplyJoin } });
    }
    return json{ { "groups", std::move(groups) } };
}

json ChatServer::createGroup(ServiceBase& me, const json& args) {
    std::string msg;
    const int lastSeq = platform_.messages().lastSeq();
    const int groupNo = GroupService::create(platform_, me, field(args, "name"), msg);
    if (groupNo == 0) throw ApiError(msg);
    persist();
    afterGroupChange(me, groupNo, {}, lastSeq);   // 推送“某某创建了群聊”
    return json{ { "groupNo", groupNo }, { "message", msg } };
}

json ChatServer::execGroup(ServiceBase& me, const json& args) {
    GroupCommand command;
    if (!GroupCommand::parseKind(field(args, "kind"), command.kind)) throw ApiError("不支持的群操作：" + field(args, "kind"));
    command.groupNo = static_cast<int>(number(args, "groupNo", 0));
    command.target = field(args, "target");
    command.text = field(args, "text");
    command.on = args.value("on", true);
    command.members = args.value("members", std::vector<std::string>());

    // 记下操作前的相关人员和已分配的最大序号：被踢、退群、被拒绝的人也要通知；之后新产生的系统提示要推送出去
    std::set<std::string> affected;
    const int lastSeq = platform_.messages().lastSeq();
    if (const Group* g = platform_.groups().find(me.type(), command.groupNo)) {
        affected.insert(g->members().begin(), g->members().end());
        affected.insert(g->pending().begin(), g->pending().end());
    }
    if (!command.target.empty()) affected.insert(command.target);
    std::string msg;
    if (!GroupService::execute(platform_, me, command, msg)) throw ApiError(msg);
    persist();
    afterGroupChange(me, command.groupNo, std::move(affected), lastSeq);
    return json{ { "message", msg } };
}

void ChatServer::afterGroupChange(ServiceBase& me, int groupNo, std::set<std::string> affected, int lastSeq) {
    const std::string service = ServiceTypeHelper::code(me.type());
    if (const Group* g = platform_.groups().find(me.type(), groupNo)) {
        affected.insert(g->members().begin(), g->members().end());
        affected.insert(g->pending().begin(), g->pending().end());
        // 操作产生的新群消息（“A 邀请 B 加入了群聊”、群公告……）推给现在的全体在线成员，包括操作者自己：
        // 这些消息不是他用 SEND_MSG_REQ 发的，不推送他就收不到。
        // 只读 lastSeq 之后的部分（不必把整个群的历史读出来）；同时会读到 me 在这之后的私信（实际上没有），跳过
        for (const Message& m : platform_.messages().messagesAfter(me.type(), me.id(), { groupNo }, lastSeq, 0)) {
            if (!m.isGroup()) continue;
            const json push = messageJson(m);
            for (const std::string& id : g->members()) pushTo(service + " " + id, MessageType::MSG_PUSH, push);
        }
    }
    // 其他相关的人重新获取群列表和打开着的群资料（操作者自己在收到响应后会重新获取）
    for (const std::string& id : affected) {
        if (id != me.id()) notifyChanged(me.type(), id, { "groups" });
    }
}

// ---- 好友：申请与验证、删除、备注、黑名单 ----

json ChatServer::socialGet(ServiceBase& me) {
    auto nick = [&](const std::string& id) {
        ServiceBase* account = platform_.findAccount(me.type(), id);
        return account ? account->nickname() : id;
    };
    json requests = json::array();
    for (const FriendRequest& r : platform_.friendRequests(me)) {   // 最新的在前
        const char* status = r.status == FriendRequest::ACCEPTED ? "accepted" : r.status == FriendRequest::REJECTED ? "rejected" : "pending";
        requests.push_back({ { "seq", r.seq }, { "from", r.from }, { "to", r.to }, { "fromNickname", nick(r.from) },
                             { "toNickname", nick(r.to) }, { "greeting", r.greeting }, { "time", r.time }, { "status", status } });
    }
    json blocked = json::array();
    for (const std::string& id : platform_.blockedList(me)) blocked.push_back({ { "id", id }, { "nickname", nick(id) } });
    return json{ { "requests", std::move(requests) }, { "verify", platform_.requiresVerification(me) }, { "blocked", std::move(blocked) } };
}

json ChatServer::profileGet(ServiceBase& me, const json& args) {
    const std::string id = field(args, "id");
    ServiceBase* other = platform_.findAccount(me.type(), id);
    if (!other) return json{ { "exists", false }, { "id", id } };
    // 多态：showInfo() 按账号的实际类型（QQ、微信……）输出各自的资料，原样转成一行一行的文字
    const json info = Util::captureLines([&] { other->showInfo(); });
    json mutual = json::array();
    for (const std::string& m : platform_.mutualFriends(me, *other)) {
        ServiceBase* account = platform_.findAccount(me.type(), m);
        mutual.push_back({ { "id", m }, { "nickname", account ? account->nickname() : m } });
    }
    const Friend* f = me.friends().find(id);
    return json{ { "exists", true },
                 { "id", id },
                 { "nickname", other->nickname() },
                 { "location", other->location() },
                 { "birthday", other->birthday().toString() },
                 { "tAge", other->tAge() },
                 { "idLabel", other->idLabel() },
                 { "info", std::move(info) },
                 { "isFriend", f != nullptr },
                 { "remark", f && f->remark() != "-" ? f->remark() : "" },
                 { "peerHasMe", other->friends().contains(me.id()) },
                 { "blocked", platform_.isBlocked(me.type(), me.id(), id) },
                 { "online", isOnline(ServiceTypeHelper::code(me.type()) + " " + id) },
                 { "mutual", std::move(mutual) } };
}

json ChatServer::requestFriend(ServiceBase& me, const json& args) {
    const std::string target = field(args, "target");
    std::string msg;
    if (!platform_.requestFriend(me, target, field(args, "greeting"), field(args, "remark"), msg)) throw ApiError(msg);
    persist();
    const bool friends = me.friends().contains(target);   // 对方没开验证：已经直接成为好友
    notifyChanged(me.type(), target, { "social", "contacts" });
    return json{ { "message", msg }, { "becameFriends", friends } };
}

json ChatServer::respondRequest(ServiceBase& me, const json& args) {
    const int seq = static_cast<int>(number(args, "seq", 0));
    std::string from;
    for (const FriendRequest& r : platform_.friendRequests(me)) {
        if (r.seq == seq) from = r.from;
    }
    std::string msg;
    if (!platform_.respondRequest(me, seq, args.value("accept", false), field(args, "remark"), msg)) throw ApiError(msg);
    persist();
    notifyChanged(me.type(), from, { "social", "contacts" });   // 申请人那边：申请状态变了，可能多了一个好友
    return json{ { "message", msg } };
}

json ChatServer::removeFriend(ServiceBase& me, const json& args) {
    const std::string id = field(args, "id");
    std::string msg;
    if (!platform_.removeFriend(me, id, msg)) throw ApiError(msg);
    persist();
    notifyChanged(me.type(), id, { "contacts" });   // 单向删除：对方列表不变，但资料页会提示“对方已把你删除”
    return json{ { "message", msg } };
}

json ChatServer::setRemark(ServiceBase& me, const json& args) {
    const std::string id = field(args, "id");
    const std::string remark = Util::toField(Util::lineText(field(args, "remark")));
    if (remark.empty()) throw ApiError("备注不能为空。");
    if (!me.friends().modifyRemark(id, remark)) throw ApiError(id + " 不是你的好友。");
    persist();
    return json{ { "message", "备注已修改" } };
}

json ChatServer::setBlocked(ServiceBase& me, const json& args) {
    std::string msg;
    if (!platform_.setBlocked(me, field(args, "id"), args.value("on", true), msg)) throw ApiError(msg);
    return json{ { "message", msg } };
}

json ChatServer::setVerification(ServiceBase& me, const json& args) {
    const bool on = args.value("on", true);
    platform_.setVerification(me, on);
    return json{ { "verify", on }, { "message", on ? "已开启好友验证" : "已关闭好友验证" } };
}

void ChatServer::notifyChanged(ServiceType type, const std::string& id, std::initializer_list<const char*> topics) {
    json list = json::array();
    for (const char* topic : topics) list.push_back(topic);
    pushTo(ServiceTypeHelper::code(type) + " " + id, MessageType::DATA_CHANGED, json{ { "topics", std::move(list) } });
}

void ChatServer::persist() {
    // 好友关系、备注保存在 friends.txt 等文件中，原来只在程序退出时写回；服务端长期运行，改完就要保存，崩溃也不丢。
    // 但一次保存要把几个文件整体重写并刷到磁盘（几毫秒到十几毫秒，大部分是 fsync），每改一次存一次，
    // 业务线程每秒只能处理一百多次这类修改。所以和消息的批量提交一样：本轮只做标记，
    // 发件箱交出回执之前统一保存一次——客户端收到"成功"时数据一定已经写入磁盘
    if (!useOutbox_) {   // 回执不经过发件箱、直接发出：只能立即保存
        saveData();
        return;
    }
    dataDirty_ = true;
    scheduleFlush();
}

void ChatServer::saveData() {
    ++stats_.dataSaves;
    if (!platform_.save()) Log::warn("保存数据文件失败（可能被其他程序改过，已另存冲突副本）");
}

void ChatServer::handleCall(Session& session, const json& request) {
    ++stats_.calls;
    json response = reply(request);
    const std::string op = field(request, "op");
    auto handler = api_.find(op);
    auto publicHandler = publicApi_.find(op);
    ServiceBase* me = accountOf(session);
    if (publicHandler == publicApi_.end() && (handler == api_.end() || !me)) {
        response["ok"] = false;
        response["error"] = "不支持的操作：" + op;
    } else {
        try {
            const auto found = request.find("args");
            const json& args = found != request.end() && found->is_object() ? *found : json::object();
            response["data"] = publicHandler != publicApi_.end() ? publicHandler->second(session, args)
                                                                 : handler->second(session, *me, args);
            response["ok"] = true;
        } catch (const ApiError& e) {
            response["ok"] = false;
            response["error"] = e.what();
        } catch (const json::exception&) {   // 参数类型不对（比如 on 传了字符串）：不能让一个畸形请求把服务端搞崩
            response["ok"] = false;
            response["error"] = "请求参数格式不正确";
        }
    }
    send(session.connection, MessageType::CALL_RESP, response);
}

json ChatServer::contactsList(ServiceBase& me) {
    const std::string service = ServiceTypeHelper::code(me.type());
    json friends = json::array();
    for (const Friend& f : me.friends().all()) {
        ServiceBase* other = platform_.findAccount(me.type(), f.id());
        json item = { { "id", f.id() },
                      { "remark", f.remark() == "-" ? "" : f.remark() },
                      { "online", isOnline(service + " " + f.id()) } };
        if (other) {
            item["nickname"] = other->nickname();
            item["location"] = other->location();
            item["birthday"] = other->birthday().toString();
            item["tAge"] = other->tAge();
            json mutual = json::array();
            for (const std::string& id : platform_.mutualFriends(me, *other)) {
                ServiceBase* m = platform_.findAccount(me.type(), id);
                mutual.push_back({ { "id", id }, { "nickname", m ? m->nickname() : id } });
            }
            item["mutual"] = std::move(mutual);
        }
        friends.push_back(std::move(item));
    }
    return json{ { "friends", std::move(friends) } };
}

json ChatServer::groupsList(ServiceBase& me) {
    json groups = json::array();
    for (const Group* g : platform_.groups().groupsOf(me.type(), me.id())) {
        const GroupPolicy& policy = g->policy();
        json members = json::array();
        json revocable = json::array();   // 我能撤回哪些成员的消息（由群当前的管理策略决定，客户端不必重复实现规则）
        for (const std::string& id : g->members()) {
            ServiceBase* account = platform_.findAccount(me.type(), id);
            members.push_back({ { "id", id }, { "nickname", account ? account->nickname() : id } });
            if (id != me.id() && policy.canRevoke(*g, me.id(), id)) revocable.push_back(id);
        }
        groups.push_back({ { "groupNo", g->groupNo() },
                           { "name", g->name() },
                           { "mode", GroupPolicy::modeCode(policy.mode()) },
                           { "modeName", policy.modeName() },
                           { "notice", g->notice() },
                           { "owner", g->ownerId() },
                           { "admins", policy.allowAdmin() ? g->admins() : std::vector<std::string>() },
                           { "muted", policy.allowMute() ? g->muted() : std::vector<std::string>() },
                           { "members", std::move(members) },
                           { "canSpeak", g->canSpeak(me.id()) },
                           { "canMentionAll", policy.canEditInfo(*g, me.id()) },
                           { "revocable", std::move(revocable) } });
    }
    return json{ { "groups", std::move(groups) } };
}

json ChatServer::readsGet(ServiceBase& me) {
    // 已读位置：我在每个会话里读到哪一条；私信的对方读到哪一条（用于显示“已读”）
    MessageStore& center = platform_.messages();
    json mine = json::object(), peers = json::object(), groups = json::object();
    for (const Friend& f : me.friends().all()) {
        mine[f.id()] = center.readMark(me.type(), "P", f.id(), me.id());
        peers[f.id()] = center.readMark(me.type(), "P", me.id(), f.id());
    }
    for (const Group* g : platform_.groups().groupsOf(me.type(), me.id()))
        groups[std::to_string(g->groupNo())] = center.groupReadMark(me.type(), g->groupNo(), me.id());
    return json{ { "private", std::move(mine) }, { "peerRead", std::move(peers) }, { "groups", std::move(groups) } };
}

json ChatServer::revokeMessage(ServiceBase& me, const json& args) {
    const int seq = static_cast<int>(number(args, "seq", 0));
    std::string error;
    if (!platform_.revokeMessage(me, seq, error)) throw ApiError(error);
    // 通知会话里其他在线的人：私信通知对方，群消息通知所有成员
    Message message(0, me.type(), false, "", "", "", "");
    platform_.messages().findMessage(me.type(), seq, message);
    const std::string service = ServiceTypeHelper::code(me.type());
    const json push = { { "seq", seq }, { "revokedBy", me.id() } };
    std::vector<std::string> receivers;
    if (!message.isGroup()) {
        receivers.push_back(message.sender() == me.id() ? message.target() : message.sender());
    } else if (const Group* g = platform_.groups().find(me.type(), std::atoi(message.target().c_str()))) {
        receivers = g->members();
    }
    for (const std::string& id : receivers) {
        if (id != me.id()) pushTo(service + " " + id, MessageType::MSG_REVOKED, push);
    }
    return json{ { "seq", seq }, { "revokedBy", me.id() } };
}

json ChatServer::messageJson(const Message& m) const {
    json body = { { "seq", m.seq() },
                  { "service", ServiceTypeHelper::code(m.service()) },
                  { "from", m.sender() },
                  { "text", m.text() },
                  { "time", m.time() } };
    if (m.isGroup()) {
        body["groupNo"] = std::atoi(m.target().c_str());
        // 这条消息 @ 了哪些成员（@昵称；能修改群资料的人发的 @所有人）
        if (const Group* g = platform_.groups().find(m.service(), std::atoi(m.target().c_str()))) {
            const std::vector<std::string> mentions = platform_.mentionsOf(*g, m);
            if (!mentions.empty()) body["mentions"] = mentions;
        }
    } else {
        body["to"] = m.target();
    }
    if (m.isSystem()) body["system"] = true;   // 群系统提示（如“A 邀请 B 加入了群聊”）
    if (m.revoked()) {                         // 撤回的消息不再下发原文
        body["revoked"] = true;
        body["revokedBy"] = m.revokedBy();
        body["text"] = "";
    }
    return body;
}

void ChatServer::notifyPresence(const std::string& accountKey, bool online) {
    const std::size_t space = accountKey.find(' ');
    ServiceType type = ServiceType::QQ;
    if (space == std::string::npos || !ServiceTypeHelper::parse(accountKey.substr(0, space), type)) return;
    const std::string id = accountKey.substr(space + 1);
    ServiceBase* me = platform_.findAccount(type, id);
    if (!me) return;
    const json push = { { "account", id }, { "online", online } };
    for (const Friend& f : me->friends().all()) pushTo(accountKey.substr(0, space) + " " + f.id(), MessageType::PRESENCE, push);
}

bool ChatServer::isOnline(const std::string& accountKey) const { return online_.count(accountKey) > 0; }

ServiceBase* ChatServer::accountOf(const Session& session) const {
    const std::size_t space = session.accountKey.find(' ');
    ServiceType type = ServiceType::QQ;
    if (space == std::string::npos || !ServiceTypeHelper::parse(session.accountKey.substr(0, space), type)) return nullptr;
    return platform_.findAccount(type, session.accountKey.substr(space + 1));
}

void ChatServer::deliver(const TcpConnection::Ptr& connection, std::string bytes) {
    if (!useOutbox_) {   // 单线程且存储不批量提交：连接就在本线程，直接发（会在本轮结束时合并写出）
        WebTransport::send(connection, std::move(bytes));
        return;
    }
    deliverShared(connection, std::make_shared<const std::string>(std::move(bytes)));
}

void ChatServer::deliverShared(const TcpConnection::Ptr& connection, const std::shared_ptr<const std::string>& bytes) {
    if (!useOutbox_) {
        WebTransport::send(connection, bytes->data(), bytes->size());
        return;
    }
    enqueue(Outgoing{ connection, bytes, nullptr });
}

void ChatServer::post(const TcpConnection::Ptr& connection, std::function<void()> task) {
    if (!useOutbox_) {
        task();
        return;
    }
    enqueue(Outgoing{ connection, nullptr, std::move(task) });
}

void ChatServer::enqueue(Outgoing item) {
    // 先攒在发件箱里，本轮业务处理完之后再交出去：
    // - 多线程：按 I/O 线程分组，每个线程只投递一次。否则群发时每条推送都是一次跨线程投递（加一次唤醒），
    //   I/O 线程每次只拿到一条，合并写也就失效了；
    // - 存储批量提交：保证先提交、再发回执
    scheduleFlush();
    outbox_.push_back(std::move(item));
}

void ChatServer::scheduleFlush() {
    if (flushScheduled_) return;
    flushScheduled_ = true;
    loop_.queueInLoop([this] { flushOutbox(); });
}

void ChatServer::Outgoing::run() const {
    // 在连接自己的线程里执行：发送时直接从共享的数据追加到输出缓冲区，只复制这一次
    if (bytes) WebTransport::send(connection, bytes->data(), bytes->size());
    else action();
}

void ChatServer::flushOutbox() {
    // 先提交本轮的写入（消息批量提交、好友和群的数据文件），再把回执、推送交出去——客户端收到回执时，数据一定已经保存
    flushScheduled_ = false;
    platform_.messages().commitPending();
    if (dataDirty_) {
        dataDirty_ = false;
        saveData();
    }
    std::vector<Outgoing> items;
    items.swap(outbox_);
    if (options_.ioThreads == 0) {   // 单线程：连接都在本线程，按顺序直接执行
        for (const Outgoing& item : items) item.run();
        return;
    }
    std::unordered_map<EventLoop*, std::shared_ptr<std::vector<Outgoing>>> batches;
    for (Outgoing& item : items) {
        auto& batch = batches[&item.connection->loop()];
        if (!batch) batch = std::make_shared<std::vector<Outgoing>>();
        batch->push_back(std::move(item));   // 同一条连接的操作保持原来的先后顺序
    }
    for (auto& [ioLoop, batch] : batches) {
        ioLoop->queueInLoop([batch] {
            for (const Outgoing& item : *batch) item.run();
        });
    }
}

void ChatServer::send(const TcpConnection::Ptr& connection, MessageType type, json body) {
    Packet packet;
    packet.type = type;
    packet.body = std::move(body);
    deliver(connection, packet.encode());
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
    // 顺便清理过期的登录失败记录：不再锁定、统计窗口也已过去的，留着只占内存
    for (auto it = throttles_.begin(); it != throttles_.end();) {
        const bool stale = it->second.lockedUntil <= now && now - it->second.windowStart > options_.loginWindow;
        it = stale ? throttles_.erase(it) : std::next(it);
    }
    // 还有离线太久的账号的消息去重表
    for (auto it = recent_.begin(); it != recent_.end();) {
        const bool stale = !isOnline(it->first) && now - it->second.lastUsed > options_.recentTtl;
        it = stale ? recent_.erase(it) : std::next(it);
    }
    std::vector<TcpConnection::Ptr> idle;   // 先收集再关闭：关闭会从 sessions_ 中删除，不能边遍历边删
    std::vector<TcpConnection::Ptr> anonymous;   // 连上以后迟迟不登录的
    for (const auto& entry : sessions_) {
        const Session& session = entry.second;
        if (now - session.connection->lastReceive() > options_.idleTimeout) idle.push_back(session.connection);
        else if (!session.loggedIn && options_.loginTimeout.count() > 0 && now - session.connectedAt > options_.loginTimeout)
            anonymous.push_back(session.connection);
    }
    for (const TcpConnection::Ptr& connection : idle) {
        Log::info("连接 " + connection->peer() + " 长时间没有心跳，断开");
        connection->forceClose();
    }
    for (const TcpConnection::Ptr& connection : anonymous) {
        Log::info("连接 " + connection->peer() + " 连上后一直没有登录，断开");
        connection->forceClose();
    }
}
