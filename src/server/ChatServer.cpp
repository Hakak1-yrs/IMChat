#include "ChatServer.h"
#include <cstdlib>
#include <optional>
#include <set>
#include <vector>
#include "Group.h"
#include "GroupPolicy.h"
#include "GroupService.h"
#include "Log.h"
#include "Platform.h"
#include "ServiceType.h"
#include "Util.h"

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

// 从末尾找 sender 发的最后一条消息：刚保存的那条（拿到它的序号和时间）
std::optional<Message> lastFrom(const std::vector<Message>& history, const std::string& sender) {
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->sender() == sender) return *it;
    }
    return std::nullopt;
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
    : loop_(loop), platform_(platform), options_(std::move(options)), server_(loop) {
    registerApi();
}

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
    const std::string accountKey = session.loggedIn ? session.accountKey : "";
    bool wentOffline = false;
    if (session.loggedIn) {
        auto online = online_.find(session.accountKey);
        // 只有它仍是该账号的当前连接时才下线：被挤下线的旧连接关闭时，不能把新登录的连接也移除
        if (online != online_.end() && online->second == connection->id()) {
            online_.erase(online);
            wentOffline = true;
        }
        Log::info(session.accountKey + " 下线");
    }
    sessions_.erase(it);
    if (wentOffline) notifyPresence(accountKey, false);
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
    case MessageType::SYNC_REQ:
    case MessageType::READ_REQ:
    case MessageType::CALL_REQ:
        if (!session.loggedIn) {
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
    response["location"] = account->location();
    response["momentsName"] = account->momentsName();
    response["unreadPrivate"] = unreadPrivate;
    response["unreadGroup"] = unreadGroup;
    send(session.connection, MessageType::LOGIN_RESP, response);
    Log::info(key + " 从 " + session.connection->peer() + " 登录");
    if (previousId == 0) notifyPresence(key, true);   // 换一处登录不算“上线”

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
    const long long groupNo = number(request, "groupNo", 0);
    if (!me || clientMsgId.empty() || (to.empty() == (groupNo == 0))) {
        sendError(session.connection, request, ErrorCode::BAD_REQUEST, "需要 clientMsgId，以及 to 或 groupNo 二者之一");
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
            saved = lastFrom(platform_.messages().groupHistory(me->type(), group->groupNo()), me->id());
            for (const std::string& member : group->members()) {
                if (member != me->id()) receivers.push_back(member);
            }
        }
    } else if (platform_.sendPrivate(*me, to, text, error)) {
        saved = lastFrom(platform_.messages().conversation(me->type(), me->id(), to), me->id());
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
    ack["ok"] = true;
    ack["seq"] = saved->seq();
    ack["time"] = saved->time();
    recent.acks[clientMsgId] = ack;
    recent.order.push_back(clientMsgId);
    if (recent.order.size() > kRecentLimit) {
        recent.acks.erase(recent.order.front());
        recent.order.pop_front();
    }
    ack.update(reply(request));
    send(session.connection, MessageType::SEND_MSG_ACK, ack);

    // 在线的接收者立即推送；不在线的上线后通过 SYNC_REQ 取到
    const json push = messageJson(*saved);
    for (const std::string& receiver : receivers) pushTo(service + " " + receiver, MessageType::MSG_PUSH, push);
}

void ChatServer::handleSync(Session& session, const json& request) {
    ServiceBase* me = accountOf(session);
    if (!me) return;
    const long long afterSeq = number(request, "afterSeq", 0);
    long long limit = number(request, "limit", kSyncDefaultLimit);
    if (limit <= 0 || limit > kSyncMaxLimit) limit = kSyncMaxLimit;

    std::vector<int> groupNos;
    for (const Group* group : platform_.groups().groupsOf(me->type(), me->id())) groupNos.push_back(group->groupNo());
    json messages = json::array();
    bool hasMore = false;
    // messagesFor 按序号从小到大排列：跳过已经有的，最多取 limit 条，剩下的让客户端接着要
    for (const Message& m : platform_.messages().messagesFor(me->type(), me->id(), groupNos)) {
        if (m.seq() <= afterSeq) continue;
        if (static_cast<long long>(messages.size()) == limit) {
            hasMore = true;
            break;
        }
        messages.push_back(messageJson(m));
    }
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
    if (receiver != sessions_.end()) send(receiver->second.connection, type, body);
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
    const int groupNo = GroupService::create(platform_, me, field(args, "name"), msg);
    if (groupNo == 0) throw ApiError(msg);
    persist();
    afterGroupChange(me, groupNo, {}, 0);   // 推送“某某创建了群聊”
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

    // 记下操作前的相关人员和最后一条群消息：被踢、退群、被拒绝的人也要通知；之后新产生的系统提示要推送出去
    std::set<std::string> affected;
    int lastSeq = 0;
    if (const Group* g = platform_.groups().find(me.type(), command.groupNo)) {
        affected.insert(g->members().begin(), g->members().end());
        affected.insert(g->pending().begin(), g->pending().end());
        const std::vector<Message> history = platform_.messages().groupHistory(me.type(), command.groupNo);
        if (!history.empty()) lastSeq = history.back().seq();
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
        // 这些消息不是他用 SEND_MSG_REQ 发的，不推送他就收不到
        for (const Message& m : platform_.messages().groupHistory(me.type(), groupNo)) {
            if (m.seq() <= lastSeq) continue;
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
    // 好友关系、备注保存在 friends.txt 等文件中，原来只在程序退出时写回；服务端长期运行，改完立即保存，崩溃也不丢
    if (!platform_.save()) Log::warn("保存数据文件失败（可能被其他程序改过，已另存冲突副本）");
}

void ChatServer::handleCall(Session& session, const json& request) {
    json response = reply(request);
    const std::string op = field(request, "op");
    auto handler = api_.find(op);
    ServiceBase* me = accountOf(session);
    if (handler == api_.end() || !me) {
        response["ok"] = false;
        response["error"] = "不支持的操作：" + op;
    } else {
        try {
            const auto args = request.find("args");
            response["data"] = handler->second(session, *me, args != request.end() && args->is_object() ? *args : json::object());
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
    MessageCenter& center = platform_.messages();
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
