#include "Session.h"
#include <algorithm>
#include <map>

Session::Session(Platform& platform) : platform_(platform) {}

bool Session::login(ServiceType type, const std::string& id, const std::string& password, std::string& msg) {
    ServiceBase* account = platform_.findAccount(type, id);
    User* owner = platform_.ownerOf(account);
    if (id.empty() || password.empty()) {
        msg = "请输入账号和密码。";
        return false;
    }
    if (!account || !owner || !account->checkPassword(password)) {
        msg = "账号或密码错误。";
        return false;
    }
    user_ = owner;
    current_ = account;
    account->login();
    msg.clear();
    for (ServiceBase* s : owner->services()) {
        if (s == account) continue;
        s->login();
        msg += (msg.empty() ? "" : "、") + s->serviceName();
    }
    if (!msg.empty()) msg = "已同步登录 " + msg;
    return true;
}

void Session::logout() {
    if (user_) user_->logoutAll();
    user_ = nullptr;
    current_ = nullptr;
}

bool Session::switchService(ServiceType type) {
    ServiceBase* target = user_ ? user_->service(type) : nullptr;
    if (!target) return false;
    target->login();
    current_ = target;
    return true;
}

bool Session::closeCurrentService(std::string& msg) {
    if (user_->services().size() == 1) {
        msg = "这是你唯一开通的服务，关闭后将无法登录。如需离开平台，请选择“注销整个账号”。";
        return false;
    }
    if (!platform_.closeService(*user_, current_->type(), msg)) return false;
    current_ = nullptr;   // 原账号对象已被删除，不能再访问
    for (ServiceBase* s : user_->services()) {
        if (!current_ || s->isLoggedIn()) current_ = s;   // 优先选仍在线的服务
    }
    current_->login();
    return true;
}

void Session::deleteCurrentUser(std::string& msg) {
    const int id = user_->id();
    user_ = nullptr;   // 用户对象即将被删除，先清空，之后不再访问
    current_ = nullptr;
    platform_.deleteUser(id, msg);
}

bool Session::loggedIn() const { return user_ != nullptr; }

User* Session::user() const { return user_; }

ServiceBase* Session::current() const { return current_; }

Platform& Session::platform() { return platform_; }

void Session::fill(Conversation& c) const {
    const ServiceType type = current_->type();
    if (c.isGroup) {
        Group* g = groupOf(c);
        c.title = g ? g->name() : "群 " + std::to_string(c.groupNo) + "（已解散）";
        c.avatarName = c.title;
        if (g) c.subtitle = std::to_string(g->members().size()) + " 人 · " + g->policy().modeName();
        return;
    }
    ServiceBase* acc = platform_.findAccount(type, c.peerId);
    const Friend* f = current_->friends().find(c.peerId);
    if (f && !f->remark().empty() && f->remark() != "-") c.title = f->remark();   // 和常见聊天软件一样优先显示备注
    else c.title = acc ? acc->nickname() : c.peerId + "(已注销)";
    c.subtitle = acc ? acc->nickname() + " · " + c.peerId : c.peerId;
    c.avatarName = acc ? acc->nickname() : c.peerId;
}

std::vector<Conversation> Session::recentChats() const {
    const ServiceType type = current_->type();
    const std::string& me = current_->id();
    std::vector<int> groupNos;
    std::map<std::string, Conversation> found;
    for (Group* g : platform_.groups().groupsOf(type, me)) {
        groupNos.push_back(g->groupNo());
        Conversation c = Conversation::groupChat(g->groupNo());
        found.emplace(c.key(), c);
    }
    MessageCenter& center = platform_.messages();
    for (const Message& m : center.messagesFor(type, me, groupNos)) {   // 按时间先后，最后一条覆盖前面的
        Conversation c = m.isGroup() ? Conversation::groupChat(std::stoi(m.target()))
                                       : Conversation::privateChat(m.sender() == me ? m.target() : m.sender());
        Conversation& entry = found.emplace(c.key(), c).first->second;
        entry.lastTime = m.time();
        entry.preview = m.text();
        if (m.revoked()) {
            entry.preview = platform_.revokeText(*current_, m);
        } else if (m.isGroup() && !m.isSystem() && m.sender() != me) {
            ServiceBase* sender = platform_.findAccount(type, m.sender());
            entry.preview = (sender ? sender->nickname() : m.sender()) + "：" + m.text();
        }
    }
    std::map<std::string, int> unreadPrivate = center.unreadPrivate(type, me);
    std::map<int, int> unreadGroups = center.unreadGroups(type, me, groupNos);
    std::vector<Conversation> result;
    for (auto& kv : found) {
        Conversation& c = kv.second;
        fill(c);
        c.unread = c.isGroup ? (unreadGroups.count(c.groupNo) ? unreadGroups[c.groupNo] : 0)
                             : (unreadPrivate.count(c.peerId) ? unreadPrivate[c.peerId] : 0);
        c.mentioned = c.isGroup && c.unread > 0 && platform_.unreadMentions(*current_, c.groupNo) > 0;
        result.push_back(c);
    }
    std::stable_sort(result.begin(), result.end(), [](const Conversation& a, const Conversation& b) {
        return a.lastTime > b.lastTime;   // “yyyy-mm-dd hh:mm:ss”可以直接按字符串比较先后
    });
    return result;
}

std::vector<Conversation> Session::contacts() const {
    std::map<std::string, int> unread = platform_.messages().unreadPrivate(current_->type(), current_->id());
    std::vector<Conversation> result;
    for (const Friend& f : current_->friends().all()) {
        Conversation c = Conversation::privateChat(f.id());
        fill(c);
        c.unread = unread.count(f.id()) ? unread[f.id()] : 0;
        result.push_back(c);
    }
    return result;
}

std::vector<Conversation> Session::myGroups() const {
    std::vector<int> groupNos;
    std::vector<Group*> groups = platform_.groups().groupsOf(current_->type(), current_->id());
    for (Group* g : groups) groupNos.push_back(g->groupNo());
    std::map<int, int> unread = platform_.messages().unreadGroups(current_->type(), current_->id(), groupNos);
    std::vector<Conversation> result;
    for (Group* g : groups) {
        Conversation c = Conversation::groupChat(g->groupNo());
        fill(c);
        c.unread = unread.count(g->groupNo()) ? unread[g->groupNo()] : 0;
        c.mentioned = c.unread > 0 && platform_.unreadMentions(*current_, g->groupNo()) > 0;
        result.push_back(c);
    }
    return result;
}

int Session::unreadOf(const ServiceBase& account) const {
    int privateCount = 0, groupCount = 0;
    platform_.unreadCount(account, privateCount, groupCount);
    return privateCount + groupCount;
}

std::vector<Message> Session::messages(const Conversation& c) const {
    MessageCenter& center = platform_.messages();
    return c.isGroup ? center.groupHistory(current_->type(), c.groupNo)
                     : center.conversation(current_->type(), current_->id(), c.peerId);
}

bool Session::canSend(const Conversation& c, std::string& reason) const {
    if (c.isGroup) {
        Group* g = groupOf(c);
        if (!g) reason = "该群已解散";
        else if (!g->isMember(current_->id())) reason = "你已不是该群成员";
        else if (!g->canSpeak(current_->id())) reason = "你已被禁言，暂时不能在本群发言";
        else return true;
        return false;
    }
    if (current_->friends().contains(c.peerId)) return true;
    reason = "对方已不是你的好友，只能查看聊天记录";
    return false;
}

bool Session::send(const Conversation& c, const std::string& text, std::string& msg) {
    if (!c.isGroup) return platform_.sendPrivate(*current_, c.peerId, text, msg);
    Group* g = groupOf(c);
    if (!g) {
        msg = "该群已解散。";
        return false;
    }
    return platform_.sendGroupMessage(*current_, *g, text, msg);
}

void Session::markRead(const Conversation& c, int seq) {
    if (seq <= 0) return;
    MessageCenter& center = platform_.messages();
    if (c.isGroup) center.markGroupRead(current_->type(), c.groupNo, current_->id(), seq);
    else center.markPrivateRead(current_->type(), current_->id(), c.peerId, seq);
}

std::string Session::titleOf(const Conversation& c) const {
    Conversation copy = c;
    fill(copy);
    return copy.title;
}

Group* Session::groupOf(const Conversation& c) const {
    return c.isGroup ? platform_.groups().find(current_->type(), c.groupNo) : nullptr;
}

bool Session::messagesChanged() {
    std::error_code ec;
    const std::filesystem::path file(platform_.dataDir() + "messages.txt");
    auto stamp = std::filesystem::last_write_time(file, ec);
    if (ec) stamp = {};
    std::uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec) size = 0;
    if (stamp == stamp_ && size == size_) return false;
    stamp_ = stamp;
    size_ = size;
    return true;
}
