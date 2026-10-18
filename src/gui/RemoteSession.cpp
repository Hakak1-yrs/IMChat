#include "RemoteSession.h"
#include <algorithm>
#include <ctime>

using json = nlohmann::json;

namespace {

// “yyyy-mm-dd hh:mm:ss”距现在多少秒（判断自己的消息是否还在 2 分钟的撤回期限内）
double secondsSince(const std::string& time) {
    std::tm t{};
    if (sscanf_s(time.c_str(), "%d-%d-%d %d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6)
        return 1e9;
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    t.tm_isdst = -1;
    return std::difftime(std::time(nullptr), std::mktime(&t));
}

std::string nowText() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

bool countsAsUnread(const Message& m, const std::string& me) { return m.sender() != me && !m.isSystem() && !m.revoked(); }

GroupMemberInfo memberFrom(const json& item) {
    GroupMemberInfo m;
    m.id = item.value("id", "");
    m.nickname = item.value("nickname", m.id);
    m.owner = item.value("owner", false);
    m.admin = item.value("admin", false);
    m.muted = item.value("muted", false);
    m.canSetAdmin = item.value("canSetAdmin", false);
    m.canMute = item.value("canMute", false);
    m.canTransfer = item.value("canTransfer", false);
    m.canKick = item.value("canKick", false);
    return m;
}

GroupInfo groupInfoFrom(const json& data) {
    GroupInfo g;
    g.groupNo = data.value("groupNo", 0);
    g.exists = data.value("exists", false);
    if (!g.exists) return g;
    g.name = data.value("name", "");
    g.mode = data.value("mode", "");
    g.modeName = data.value("modeName", "");
    g.owner = data.value("owner", "");
    g.notice = data.value("notice", "");
    g.preset = data.value("preset", false);
    g.features = data.value("features", std::vector<std::string>());
    for (const json& item : data.value("members", json::array())) g.members.push_back(memberFrom(item));
    for (const json& item : data.value("pending", json::array())) g.pending.push_back(memberFrom(item));
    for (const json& item : data.value("subGroups", json::array())) {
        SubGroupInfo s;
        s.name = item.value("name", "");
        s.creator = item.value("creator", "");
        s.members = item.value("members", std::vector<std::string>());
        s.canAdd = item.value("canAdd", false);
        s.canDissolve = item.value("canDissolve", false);
        g.subGroups.push_back(s);
    }
    g.frozenAdmins = data.value("frozenAdmins", 0);
    g.frozenMuted = data.value("frozenMuted", 0);
    g.member = data.value("member", false);
    g.isOwner = data.value("isOwner", false);
    g.isAdmin = data.value("isAdmin", false);
    g.applied = data.value("applied", false);
    g.canEditInfo = data.value("canEditInfo", false);
    g.allowApplyJoin = data.value("allowApplyJoin", false);
    g.allowSubGroup = data.value("allowSubGroup", false);
    g.allowAdmin = data.value("allowAdmin", false);
    g.allowMute = data.value("allowMute", false);
    g.canApprove = data.value("canApprove", false);
    g.canDissolve = data.value("canDissolve", false);
    return g;
}

}  // namespace

RemoteSession::RemoteSession(std::string host, std::uint16_t port) : host_(std::move(host)), port_(port) {}

RemoteSession::~RemoteSession() {
    if (client_) client_->stop();   // 先停网络线程，之后不会再有回调往收件箱里放东西
}

void RemoteSession::start(ServiceType type, const std::string& account, const std::string& password) {
    me_.type = type;
    me_.id = account;
    status_ = "正在连接服务器 " + host_ + ":" + std::to_string(port_) + "…";
    client_ = std::make_unique<ChatClient>(callbacks());
    client_->start(host_, port_);
    client_->login(ServiceTypeHelper::code(type), account, password);
}

ChatClient::Callbacks RemoteSession::callbacks() {
    ChatClient::Callbacks c;
    c.onState = [this](ChatClient::State state) {
        post([this, state] {
            if (state == ChatClient::State::LOGGED_IN) {
                status_.clear();
            } else if (state == ChatClient::State::CONNECTING && loggedIn_) {
                status_ = "与服务器的连接已断开，正在重新连接…";
                // 断线期间不知道谁在线：先都显示为离线，重连后重新取联系人时恢复
                for (auto& entry : friends_) entry.second.online = false;
            }
        });
    };
    c.onLogin = [this](bool ok, const json& response) { post([this, ok, response] { onLogin(ok, response); }); };
    c.onMessage = [this](const json& message, bool) { post([this, message] { applyMessage(message); }); };
    c.onAck = [this](const json& ack) { post([this, ack] { applyAck(ack); }); };
    c.onReadReceipt = [this](const json& receipt) {
        post([this, receipt] {
            int& seq = peerRead_[receipt.value("reader", "")];
            seq = std::max(seq, receipt.value("seq", 0));
        });
    };
    c.onKicked = [this](const std::string& reason) {
        post([this, reason] {
            loggedIn_ = false;   // 界面发现后回到登录页
            notice_ = reason;
        });
    };
    c.onPush = [this](MessageType type, const json& body) {
        post([this, type, body] {
            if (type == MessageType::PRESENCE) {
                auto it = friends_.find(body.value("account", ""));
                if (it != friends_.end()) it->second.online = body.value("online", false);
            } else if (type == MessageType::MSG_REVOKED) {
                auto it = messages_.find(body.value("seq", 0));
                if (it != messages_.end()) it->second.markRevoked(body.value("revokedBy", ""));
            } else if (type == MessageType::DATA_CHANGED) {   // 别人的操作影响了我（如通过了我的好友申请）
                for (const std::string& topic : body.value("topics", std::vector<std::string>())) reload(topic);
            }
        });
    };
    return c;
}

void RemoteSession::post(std::function<void()> change) {
    std::lock_guard<std::mutex> lock(mutex_);
    inbox_.push_back(std::move(change));
}

void RemoteSession::drain() {
    std::vector<std::function<void()>> changes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        changes.swap(inbox_);
    }
    for (auto& change : changes) change();
    if (!changes.empty()) dirty_ = true;
}

RemoteSession::LoginState RemoteSession::loginState() {
    drain();
    return loginState_;
}

std::string RemoteSession::loginError() const { return loginError_; }

std::string RemoteSession::server() const { return host_ + ":" + std::to_string(port_); }

void RemoteSession::onLogin(bool ok, const json& response) {
    if (!ok) {
        if (loginState_ != LoginState::READY) {   // 首次登录失败；重连后的自动登录失败不会走到这里（密码不会变）
            loginState_ = LoginState::FAILED;
            loginError_ = response.value("error", "登录失败");
        }
        return;
    }
    me_.nickname = response.value("nickname", me_.id);
    me_.location = response.value("location", "");
    me_.momentsName = response.value("momentsName", "动态");
    loggedIn_ = true;
    if (loginState_ != LoginState::READY) loginState_ = LoginState::LOADING;
    loadLists();   // 断线重连后也重新取一遍：断线期间好友、群可能有变化
}

void RemoteSession::loadLists() {
    listsLoading_ = 4;
    fetch("contacts.list", json::object(), [this](const json& d) { applyContacts(d); }, true);
    fetch("groups.list", json::object(), [this](const json& d) { applyGroups(d); }, true);
    fetch("reads.get", json::object(), [this](const json& d) { applyReads(d); }, true);
    fetch("social.get", json::object(), [this](const json& d) { applySocial(d); }, true);
    // 断线重连后，打开过的资料、群详情、全部群也刷新一遍
    for (const auto& entry : profiles_) loadProfile(entry.first);
    for (const auto& entry : groupInfos_) loadGroup(entry.first);
    if (allGroupsWanted_) loadAllGroups();
}

void RemoteSession::listLoaded() {
    if (--listsLoading_ == 0 && loginState_ == LoginState::LOADING) loginState_ = LoginState::READY;
}

void RemoteSession::fetch(const char* op, json args, std::function<void(const json&)> apply, bool initial) {
    client_->call(op, std::move(args), [this, apply, initial](bool ok, const json& data, const std::string& error) {
        post([this, apply, initial, ok, data, error] {
            if (ok) apply(data);
            else if (!initial) notice_ = error;   // 首次加载失败不打扰（多半是断线，重连后会重新加载）
            if (initial) listLoaded();
        });
    });
}

void RemoteSession::reload(const std::string& topic) {
    if (topic == "contacts") {
        fetch("contacts.list", json::object(), [this](const json& d) { applyContacts(d); }, false);
        for (const auto& entry : profiles_) loadProfile(entry.first);   // 好友关系变了，资料页上的“是否好友”等也要更新
    } else if (topic == "social") {
        fetch("social.get", json::object(), [this](const json& d) { applySocial(d); }, false);
    } else if (topic == "groups") {   // 群成员、权限、群名……变了：我的群列表、打开过的群详情、全部群都重新取
        fetch("groups.list", json::object(), [this](const json& d) { applyGroups(d); }, false);
        for (const auto& entry : groupInfos_) loadGroup(entry.first);
        if (allGroupsWanted_) loadAllGroups();
    }
}

void RemoteSession::mutate(const char* op, json args, std::vector<std::string> topics, const std::string& profileId, Done done) {
    client_->call(op, std::move(args), [this, topics, profileId, done](bool ok, const json& data, const std::string& error) {
        post([this, topics, profileId, done, ok, data, error] {
            if (ok) {
                for (const std::string& topic : topics) reload(topic);
                if (!profileId.empty()) loadProfile(profileId);
            }
            if (done) done(ok, ok ? data.value("message", "") : error);
        });
    });
}

void RemoteSession::applySocial(const json& data) {
    requests_.clear();
    for (const json& item : data.value("requests", json::array())) {
        FriendRequestView view;
        view.seq = item.value("seq", 0);
        view.from = item.value("from", "");
        view.to = item.value("to", "");
        view.greeting = item.value("greeting", "");
        view.time = item.value("time", "");
        const std::string status = item.value("status", "pending");
        view.status = status == "accepted" ? FriendRequestView::ACCEPTED
                      : status == "rejected" ? FriendRequestView::REJECTED
                                             : FriendRequestView::PENDING;
        nicknames_[view.from] = item.value("fromNickname", view.from);
        nicknames_[view.to] = item.value("toNickname", view.to);
        requests_.push_back(view);
    }
    verify_ = data.value("verify", true);
    blocked_.clear();
    for (const json& item : data.value("blocked", json::array())) {
        blocked_.push_back(item.value("id", ""));
        nicknames_[item.value("id", "")] = item.value("nickname", "");
    }
}

void RemoteSession::applyProfile(const json& data) {
    ProfileView view;
    view.id = data.value("id", "");
    view.known = data.value("exists", false);
    if (view.known) {
        view.nickname = data.value("nickname", view.id);
        view.location = data.value("location", "");
        view.birthday = data.value("birthday", "");
        view.tAge = data.value("tAge", 0);
        view.idLabel = data.value("idLabel", "");
        view.info = data.value("info", std::vector<std::string>());
        view.isFriend = data.value("isFriend", false);
        view.remark = data.value("remark", "");
        view.peerHasMe = data.value("peerHasMe", true);
        view.blocked = data.value("blocked", false);
        view.showOnline = true;
        view.online = data.value("online", false);
        for (const json& m : data.value("mutual", json::array())) {
            view.mutualFriends.push_back(m.value("id", ""));
            nicknames_[m.value("id", "")] = m.value("nickname", "");
        }
        nicknames_[view.id] = view.nickname;
    }
    profiles_[view.id] = view;
}

void RemoteSession::applyContacts(const json& data) {
    friendOrder_.clear();
    friends_.clear();
    for (const json& item : data.value("friends", json::array())) {
        FriendEntry entry;
        const std::string id = item.value("id", "");
        entry.nickname = item.value("nickname", id);
        entry.remark = item.value("remark", "");
        entry.location = item.value("location", "");
        entry.birthday = item.value("birthday", "");
        entry.tAge = item.value("tAge", 0);
        entry.online = item.value("online", false);
        for (const json& m : item.value("mutual", json::array())) {
            entry.mutual.push_back(m.value("id", ""));
            nicknames_[m.value("id", "")] = m.value("nickname", "");
        }
        nicknames_[id] = entry.nickname;
        friendOrder_.push_back(id);
        friends_[id] = entry;
    }
}

void RemoteSession::applyGroups(const json& data) {
    groupOrder_.clear();
    groups_.clear();
    for (const json& item : data.value("groups", json::array())) {
        GroupEntry entry;
        GroupView& view = entry.view;
        view.groupNo = item.value("groupNo", 0);
        view.name = item.value("name", "");
        view.modeName = item.value("modeName", "");
        view.notice = item.value("notice", "");
        view.canMentionAll = item.value("canMentionAll", false);
        const std::string owner = item.value("owner", "");
        const std::vector<std::string> admins = item.value("admins", std::vector<std::string>());
        const std::vector<std::string> muted = item.value("muted", std::vector<std::string>());
        for (const json& m : item.value("members", json::array())) {
            MemberView member;
            member.id = m.value("id", "");
            member.nickname = m.value("nickname", member.id);
            member.owner = member.id == owner;
            member.admin = std::find(admins.begin(), admins.end(), member.id) != admins.end();
            member.muted = std::find(muted.begin(), muted.end(), member.id) != muted.end();
            nicknames_[member.id] = member.nickname;
            view.members.push_back(member);
        }
        entry.canSpeak = item.value("canSpeak", true);
        for (const std::string& id : item.value("revocable", std::vector<std::string>())) entry.revocable.insert(id);
        groupOrder_.push_back(view.groupNo);
        groups_[view.groupNo] = entry;
    }
}

void RemoteSession::applyReads(const json& data) {
    // 先存到局部变量再遍历：items() 返回的是对对象的引用视图，
    // 直接写 data.value(...).items()，value() 返回的临时对象在循环开始前就已销毁
    const json mine = data.value("private", json::object());
    const json groups = data.value("groups", json::object());
    const json peers = data.value("peerRead", json::object());
    for (const auto& [peer, seq] : mine.items()) {
        int& mark = readMarks_[Conversation::privateChat(peer).key()];
        mark = std::max(mark, seq.get<int>());
    }
    for (const auto& [groupNo, seq] : groups.items()) {
        int& mark = readMarks_[Conversation::groupChat(std::atoi(groupNo.c_str())).key()];
        mark = std::max(mark, seq.get<int>());
    }
    for (const auto& [peer, seq] : peers.items()) peerRead_[peer] = std::max(peerRead_[peer], seq.get<int>());
}

void RemoteSession::applyMessage(const json& m) {
    ServiceType service = me_.type;
    ServiceTypeHelper::parse(m.value("service", ""), service);
    const bool isGroup = m.contains("groupNo");
    const std::string target = isGroup ? std::to_string(m.value("groupNo", 0)) : m.value("to", "");
    const std::string sender = m.value("system", false) ? Message::systemSender() : m.value("from", "");
    Message message(m.value("seq", 0), service, isGroup, target, sender, m.value("time", ""), m.value("text", ""));
    if (m.value("revoked", false)) message.markRevoked(m.value("revokedBy", ""));
    for (const std::string& id : m.value("mentions", std::vector<std::string>())) {
        if (id == me_.id) mentionsMe_.insert(message.seq());
    }
    messages_.insert_or_assign(message.seq(), message);
}

void RemoteSession::applyAck(const json& ack) {
    auto it = std::find_if(pending_.begin(), pending_.end(),
                           [&](const PendingEntry& p) { return p.clientMsgId == ack.value("clientMsgId", ""); });
    if (it == pending_.end()) return;
    if (!ack.value("ok", false)) {   // 被服务端拒绝：留在界面上标红，显示原因
        it->view.failed = true;
        it->view.error = ack.value("error", "发送失败");
        return;
    }
    // 服务端不会把我自己的消息再推给我：用回执里的序号和时间，在本地补上这条消息
    const bool isGroup = it->key.compare(0, 2, "G:") == 0;
    const std::string target = it->key.substr(2);
    Message message(ack.value("seq", 0), me_.type, isGroup, target, me_.id, ack.value("time", nowText()), it->view.text);
    messages_.insert_or_assign(message.seq(), message);
    pending_.erase(it);
}

// ======================== Session 接口 ========================

bool RemoteSession::isRemote() const { return true; }

bool RemoteSession::loggedIn() const { return loggedIn_ && loginState_ == LoginState::READY; }

void RemoteSession::logout() {
    if (client_) client_->stop();
    loggedIn_ = false;
}

AccountView RemoteSession::me() const { return me_; }

std::vector<ServiceType> RemoteSession::services() const { return { me_.type }; }   // 联网模式目前一次只登录一个服务

bool RemoteSession::switchService(ServiceType type) { return type == me_.type; }

int RemoteSession::unreadOf(ServiceType type) const {
    if (type != me_.type) return 0;
    int total = 0;
    for (const Conversation& c : recentChats()) total += c.unread;
    return total;
}

std::string RemoteSession::keyOf(const Message& m) const {
    if (m.isGroup()) return Conversation::groupChat(std::atoi(m.target().c_str())).key();
    return Conversation::privateChat(m.sender() == me_.id ? m.target() : m.sender()).key();
}

const RemoteSession::GroupEntry* RemoteSession::groupEntry(int groupNo) const {
    auto it = groups_.find(groupNo);
    return it == groups_.end() ? nullptr : &it->second;
}

void RemoteSession::fill(Conversation& c) const {
    if (c.isGroup) {
        const GroupEntry* g = groupEntry(c.groupNo);
        c.title = g ? g->view.name : "群 " + std::to_string(c.groupNo);
        c.avatarName = c.title;
        if (g) c.subtitle = std::to_string(g->view.members.size()) + " 人 · " + g->view.modeName;
        return;
    }
    auto f = friends_.find(c.peerId);
    const std::string nickname = nicknameOf(c.peerId);
    c.title = f != friends_.end() && !f->second.remark.empty() ? f->second.remark : nickname;
    c.avatarName = nickname;
    c.subtitle = nickname + " · " + c.peerId;
    if (f != friends_.end()) c.subtitle += f->second.online ? " · 在线" : " · 离线";
}

int RemoteSession::unreadIn(const std::string& key, bool* mentioned) const {
    auto mark = readMarks_.find(key);
    const int read = mark == readMarks_.end() ? 0 : mark->second;
    int count = 0;
    for (auto it = messages_.upper_bound(read); it != messages_.end(); ++it) {
        if (keyOf(it->second) != key || !countsAsUnread(it->second, me_.id)) continue;
        ++count;
        if (mentioned && mentionsMe_.count(it->first)) *mentioned = true;
    }
    return count;
}

std::vector<Conversation> RemoteSession::recentChats() const {
    std::map<std::string, Conversation> found;
    for (int groupNo : groupOrder_) {
        Conversation c = Conversation::groupChat(groupNo);
        found.emplace(c.key(), c);
    }
    for (const auto& [seq, m] : messages_) {   // 按序号递增，后面的覆盖前面的预览
        if (m.isGroup() && !groupEntry(std::atoi(m.target().c_str()))) continue;   // 已经退出的群
        const std::string key = keyOf(m);
        Conversation c = m.isGroup() ? Conversation::groupChat(std::atoi(m.target().c_str()))
                                     : Conversation::privateChat(m.sender() == me_.id ? m.target() : m.sender());
        Conversation& entry = found.emplace(key, c).first->second;
        entry.lastTime = m.time();
        entry.preview = m.revoked() ? revokeText(m)
                        : (m.isGroup() && !m.isSystem() && m.sender() != me_.id) ? nicknameOf(m.sender()) + "：" + m.text()
                                                                                    : m.text();
    }
    for (const PendingEntry& p : pending_) {   // 还在发送中的消息也显示在预览里
        auto it = found.find(p.key);
        if (it == found.end()) continue;
        it->second.preview = (p.view.failed ? "[发送失败] " : "[发送中] ") + p.view.text;
    }
    std::vector<Conversation> result;
    for (auto& [key, c] : found) {
        fill(c);
        c.unread = unreadIn(key, &c.mentioned);
        result.push_back(c);
    }
    std::stable_sort(result.begin(), result.end(), [](const Conversation& a, const Conversation& b) { return a.lastTime > b.lastTime; });
    return result;
}

std::vector<Conversation> RemoteSession::contacts() const {
    std::vector<Conversation> result;
    for (const std::string& id : friendOrder_) {
        Conversation c = Conversation::privateChat(id);
        fill(c);
        c.unread = unreadIn(c.key(), nullptr);
        result.push_back(c);
    }
    return result;
}

std::vector<Conversation> RemoteSession::myGroups() const {
    std::vector<Conversation> result;
    for (int groupNo : groupOrder_) {
        Conversation c = Conversation::groupChat(groupNo);
        fill(c);
        c.unread = unreadIn(c.key(), &c.mentioned);
        result.push_back(c);
    }
    return result;
}

std::vector<Message> RemoteSession::messages(const Conversation& c) const {
    std::vector<Message> result;
    const std::string key = c.key();
    for (const auto& [seq, m] : messages_) {
        if (keyOf(m) == key) result.push_back(m);
    }
    return result;
}

std::vector<PendingView> RemoteSession::pending(const Conversation& c) const {
    std::vector<PendingView> result;
    for (const PendingEntry& p : pending_) {
        if (p.key == c.key()) result.push_back(p.view);
    }
    return result;
}

int RemoteSession::peerReadSeq(const Conversation& c) const {
    if (c.isGroup) return 0;
    auto it = peerRead_.find(c.peerId);
    return it == peerRead_.end() ? 0 : it->second;
}

bool RemoteSession::canSend(const Conversation& c, std::string& reason) const {
    // 断线时也可以发：消息进入待确认队列，重连后自动补发
    if (c.isGroup) {
        const GroupEntry* g = groupEntry(c.groupNo);
        if (!g) reason = "你已不是该群成员";
        else if (!g->canSpeak) reason = "你已被禁言，暂时不能在本群发言";
        else return true;
        return false;
    }
    if (friends_.count(c.peerId)) return true;
    reason = "对方已不是你的好友，只能查看聊天记录";
    return false;
}

bool RemoteSession::send(const Conversation& c, const std::string& text, std::string&) {
    PendingEntry entry;
    entry.clientMsgId = c.isGroup ? client_->sendGroupText(c.groupNo, text) : client_->sendText(c.peerId, text);
    entry.key = c.key();
    entry.view.text = text;
    pending_.push_back(entry);
    dirty_ = true;
    return true;   // 结果（成功的序号或失败原因）稍后由回执带回
}

void RemoteSession::markRead(const Conversation& c, int seq) {
    int& mark = readMarks_[c.key()];
    if (seq <= mark) return;
    mark = seq;
    if (c.isGroup) client_->markGroupRead(c.groupNo, seq);
    else client_->markRead(c.peerId, seq);
}

std::string RemoteSession::titleOf(const Conversation& c) const {
    Conversation copy = c;
    fill(copy);
    return copy.title;
}

std::optional<GroupView> RemoteSession::group(const Conversation& c) const {
    const GroupEntry* g = c.isGroup ? groupEntry(c.groupNo) : nullptr;
    if (!g) return std::nullopt;
    return g->view;
}

ProfileView RemoteSession::profile(const std::string& id) const {
    ProfileView view;
    view.id = id;
    auto cached = profiles_.find(id);
    if (cached != profiles_.end()) view = cached->second;   // 资料页取回的完整资料
    auto it = friends_.find(id);
    if (it == friends_.end()) return view;
    // 好友：在线状态、备注以联系人列表为准（它们靠推送和重新加载保持最新）
    const FriendEntry& f = it->second;
    view.known = true;
    view.isFriend = true;
    view.remark = f.remark;
    view.showOnline = true;
    view.online = f.online;
    if (cached == profiles_.end()) {
        view.nickname = f.nickname;
        view.location = f.location;
        view.birthday = f.birthday;
        view.tAge = f.tAge;
        view.mutualFriends = f.mutual;
    }
    return view;
}

std::vector<FriendRequestView> RemoteSession::friendRequests() const { return requests_; }

bool RemoteSession::requiresVerification() const { return verify_; }

std::vector<std::string> RemoteSession::blockedList() const { return blocked_; }

int RemoteSession::pendingFriendRequests() const {
    int count = 0;
    for (const FriendRequestView& r : requests_) count += r.to == me_.id && r.status == FriendRequestView::PENDING ? 1 : 0;
    return count;
}

void RemoteSession::loadProfile(const std::string& id) {
    fetch("profile.get", json{ { "id", id } }, [this](const json& d) { applyProfile(d); }, false);
}

void RemoteSession::requestFriend(const std::string& target, const std::string& greeting, const std::string& remark, Done done) {
    mutate("friends.request", json{ { "target", target }, { "greeting", greeting }, { "remark", remark } }, { "contacts", "social" },
           target, std::move(done));
}

void RemoteSession::respondRequest(int seq, bool accept, const std::string& remark, Done done) {
    mutate("friends.respond", json{ { "seq", seq }, { "accept", accept }, { "remark", remark } }, { "contacts", "social" }, "",
           std::move(done));
}

void RemoteSession::removeFriend(const std::string& id, Done done) {
    mutate("friends.remove", json{ { "id", id } }, { "contacts" }, id, std::move(done));
}

void RemoteSession::setRemark(const std::string& id, const std::string& remark, Done done) {
    mutate("friends.remark", json{ { "id", id }, { "remark", remark } }, { "contacts" }, id, std::move(done));
}

void RemoteSession::setBlocked(const std::string& id, bool on, Done done) {
    mutate("friends.block", json{ { "id", id }, { "on", on } }, { "social" }, id, std::move(done));
}

void RemoteSession::setVerification(bool on, Done done) {
    mutate("settings.verify", json{ { "on", on } }, { "social" }, "", std::move(done));
}

std::vector<GroupBrief> RemoteSession::allGroups() const { return allGroups_; }

std::optional<GroupInfo> RemoteSession::groupInfo(int groupNo) const {
    auto it = groupInfos_.find(groupNo);
    if (it == groupInfos_.end()) return std::nullopt;
    return it->second;
}

void RemoteSession::loadAllGroups() {
    allGroupsWanted_ = true;   // 以后群有变化时也刷新它
    fetch("groups.all", json::object(), [this](const json& data) {
        allGroups_.clear();
        for (const json& item : data.value("groups", json::array())) {
            GroupBrief g;
            g.groupNo = item.value("groupNo", 0);
            g.name = item.value("name", "");
            g.modeName = item.value("modeName", "");
            g.owner = item.value("owner", "");
            g.ownerName = item.value("ownerName", "");
            g.memberCount = item.value("memberCount", 0);
            g.joined = item.value("joined", false);
            g.applied = item.value("applied", false);
            g.allowApplyJoin = item.value("allowApplyJoin", false);
            allGroups_.push_back(g);
        }
    }, false);
}

void RemoteSession::loadGroup(int groupNo) {
    fetch("group.get", json{ { "groupNo", groupNo } }, [this](const json& data) {
        GroupInfo g = groupInfoFrom(data);
        for (const GroupMemberInfo& m : g.members) nicknames_[m.id] = m.nickname;
        for (const GroupMemberInfo& m : g.pending) nicknames_[m.id] = m.nickname;
        groupInfos_[g.groupNo] = std::move(g);
    }, false);
}

void RemoteSession::createGroup(const std::string& name, Created done) {
    client_->call("group.create", json{ { "name", name } }, [this, done](bool ok, const json& data, const std::string& error) {
        post([this, done, ok, data, error] {
            if (ok) reload("groups");
            if (done) done(ok, ok ? data.value("message", "") : error, ok ? data.value("groupNo", 0) : 0);
        });
    });
}

void RemoteSession::groupCommand(const GroupCommand& command, Done done) {
    const json args = { { "kind", GroupCommand::kindName(command.kind) }, { "groupNo", command.groupNo }, { "target", command.target },
                        { "text", command.text }, { "on", command.on }, { "members", command.members } };
    mutate("group.exec", args, { "groups" }, "", std::move(done));
}

std::string RemoteSession::nicknameOf(const std::string& id) const {
    if (id == me_.id) return me_.nickname;
    auto it = nicknames_.find(id);
    return it == nicknames_.end() || it->second.empty() ? id : it->second;
}

bool RemoteSession::mentionsMe(const Message& m) const { return mentionsMe_.count(m.seq()) > 0; }

bool RemoteSession::canRevoke(const Message& m) const {
    if (m.revoked() || m.isSystem()) return false;
    if (m.sender() == me_.id) return secondsSince(m.time()) <= 120;   // 自己的消息：2 分钟内
    const GroupEntry* g = m.isGroup() ? groupEntry(std::atoi(m.target().c_str())) : nullptr;
    return g && g->revocable.count(m.sender()) > 0;   // 群主、管理员撤回成员的消息
}

void RemoteSession::revoke(const Message& m) {
    const int seq = m.seq();
    client_->call("message.revoke", json{ { "seq", seq } }, [this, seq](bool ok, const json&, const std::string& error) {
        post([this, ok, seq, error] {
            if (!ok) {
                notice_ = error;
                return;
            }
            auto it = messages_.find(seq);
            if (it != messages_.end()) it->second.markRevoked(me_.id);
        });
    });
}

std::string RemoteSession::revokeText(const Message& m) const {
    auto who = [&](const std::string& id) { return id == me_.id ? std::string("你") : "“" + nicknameOf(id) + "”"; };
    const std::string& by = m.revokedBy();
    if (by == m.sender()) return who(by) + "撤回了一条消息";
    return who(by) + "撤回了" + who(m.sender()) + "的一条消息";
}

bool RemoteSession::changed() {
    drain();
    const bool result = dirty_;
    dirty_ = false;
    return result;
}

std::string RemoteSession::takeNotice() {
    std::string notice;
    notice.swap(notice_);
    return notice;
}

std::string RemoteSession::status() const { return status_; }
