#include "SessionYD.h"
#include <algorithm>
#include <map>

SessionYD::SessionYD(PlatformYD& platform) : platform_(platform) {}

bool SessionYD::login(ServiceTypeYD type, const std::string& id, const std::string& password, std::string& msg) {
    ServiceBaseYD* account = platform_.findAccount(type, id);
    UserYD* owner = platform_.ownerOf(account);
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
    for (ServiceBaseYD* s : owner->services()) {
        if (s == account) continue;
        s->login();
        msg += (msg.empty() ? "" : "、") + s->serviceName();
    }
    if (!msg.empty()) msg = "已同步登录 " + msg;
    return true;
}

void SessionYD::logout() {
    if (user_) user_->logoutAll();
    user_ = nullptr;
    current_ = nullptr;
}

bool SessionYD::switchService(ServiceTypeYD type) {
    ServiceBaseYD* target = user_ ? user_->service(type) : nullptr;
    if (!target) return false;
    target->login();
    current_ = target;
    return true;
}

bool SessionYD::closeCurrentService(std::string& msg) {
    if (user_->services().size() == 1) {
        msg = "这是你唯一开通的服务，关闭后将无法登录。如需离开平台，请选择“注销整个账号”。";
        return false;
    }
    if (!platform_.closeService(*user_, current_->type(), msg)) return false;
    current_ = nullptr;   // 原账号对象已被删除，不能再访问
    for (ServiceBaseYD* s : user_->services()) {
        if (!current_ || s->isLoggedIn()) current_ = s;   // 优先选仍在线的服务
    }
    current_->login();
    return true;
}

void SessionYD::deleteCurrentUser(std::string& msg) {
    const int id = user_->id();
    user_ = nullptr;   // 用户对象即将被删除，先清空，之后不再访问
    current_ = nullptr;
    platform_.deleteUser(id, msg);
}

bool SessionYD::loggedIn() const { return user_ != nullptr; }

UserYD* SessionYD::user() const { return user_; }

ServiceBaseYD* SessionYD::current() const { return current_; }

PlatformYD& SessionYD::platform() { return platform_; }

void SessionYD::fill(ConversationYD& c) const {
    const ServiceTypeYD type = current_->type();
    if (c.isGroup) {
        GroupYD* g = groupOf(c);
        c.title = g ? g->name() : "群 " + std::to_string(c.groupNo) + "（已解散）";
        c.avatarName = c.title;
        if (g) c.subtitle = std::to_string(g->members().size()) + " 人 · " + g->policy().modeName();
        return;
    }
    ServiceBaseYD* acc = platform_.findAccount(type, c.peerId);
    const FriendYD* f = current_->friends().find(c.peerId);
    if (f && !f->remark().empty() && f->remark() != "-") c.title = f->remark();   // 和常见聊天软件一样优先显示备注
    else c.title = acc ? acc->nickname() : c.peerId + "(已注销)";
    c.subtitle = acc ? acc->nickname() + " · " + c.peerId : c.peerId;
    c.avatarName = acc ? acc->nickname() : c.peerId;
}

std::vector<ConversationYD> SessionYD::recentChats() const {
    const ServiceTypeYD type = current_->type();
    const std::string& me = current_->id();
    std::vector<int> groupNos;
    std::map<std::string, ConversationYD> found;
    for (GroupYD* g : platform_.groups().groupsOf(type, me)) {
        groupNos.push_back(g->groupNo());
        ConversationYD c = ConversationYD::groupChat(g->groupNo());
        found.emplace(c.key(), c);
    }
    MessageCenterYD& center = platform_.messages();
    for (const MessageYD& m : center.messagesFor(type, me, groupNos)) {   // 按时间先后，最后一条覆盖前面的
        ConversationYD c = m.isGroup() ? ConversationYD::groupChat(std::stoi(m.target()))
                                       : ConversationYD::privateChat(m.sender() == me ? m.target() : m.sender());
        ConversationYD& entry = found.emplace(c.key(), c).first->second;
        entry.lastTime = m.time();
        entry.preview = m.text();
        if (m.revoked()) {
            entry.preview = platform_.revokeText(*current_, m);
        } else if (m.isGroup() && !m.isSystem() && m.sender() != me) {
            ServiceBaseYD* sender = platform_.findAccount(type, m.sender());
            entry.preview = (sender ? sender->nickname() : m.sender()) + "：" + m.text();
        }
    }
    std::map<std::string, int> unreadPrivate = center.unreadPrivate(type, me);
    std::map<int, int> unreadGroups = center.unreadGroups(type, me, groupNos);
    std::vector<ConversationYD> result;
    for (auto& kv : found) {
        ConversationYD& c = kv.second;
        fill(c);
        c.unread = c.isGroup ? (unreadGroups.count(c.groupNo) ? unreadGroups[c.groupNo] : 0)
                             : (unreadPrivate.count(c.peerId) ? unreadPrivate[c.peerId] : 0);
        c.mentioned = c.isGroup && c.unread > 0 && platform_.unreadMentions(*current_, c.groupNo) > 0;
        result.push_back(c);
    }
    std::stable_sort(result.begin(), result.end(), [](const ConversationYD& a, const ConversationYD& b) {
        return a.lastTime > b.lastTime;   // “yyyy-mm-dd hh:mm:ss”可以直接按字符串比较先后
    });
    return result;
}

std::vector<ConversationYD> SessionYD::contacts() const {
    std::map<std::string, int> unread = platform_.messages().unreadPrivate(current_->type(), current_->id());
    std::vector<ConversationYD> result;
    for (const FriendYD& f : current_->friends().all()) {
        ConversationYD c = ConversationYD::privateChat(f.id());
        fill(c);
        c.unread = unread.count(f.id()) ? unread[f.id()] : 0;
        result.push_back(c);
    }
    return result;
}

std::vector<ConversationYD> SessionYD::myGroups() const {
    std::vector<int> groupNos;
    std::vector<GroupYD*> groups = platform_.groups().groupsOf(current_->type(), current_->id());
    for (GroupYD* g : groups) groupNos.push_back(g->groupNo());
    std::map<int, int> unread = platform_.messages().unreadGroups(current_->type(), current_->id(), groupNos);
    std::vector<ConversationYD> result;
    for (GroupYD* g : groups) {
        ConversationYD c = ConversationYD::groupChat(g->groupNo());
        fill(c);
        c.unread = unread.count(g->groupNo()) ? unread[g->groupNo()] : 0;
        c.mentioned = c.unread > 0 && platform_.unreadMentions(*current_, g->groupNo()) > 0;
        result.push_back(c);
    }
    return result;
}

int SessionYD::unreadOf(const ServiceBaseYD& account) const {
    int privateCount = 0, groupCount = 0;
    platform_.unreadCount(account, privateCount, groupCount);
    return privateCount + groupCount;
}

std::vector<MessageYD> SessionYD::messages(const ConversationYD& c) const {
    MessageCenterYD& center = platform_.messages();
    return c.isGroup ? center.groupHistory(current_->type(), c.groupNo)
                     : center.conversation(current_->type(), current_->id(), c.peerId);
}

bool SessionYD::canSend(const ConversationYD& c, std::string& reason) const {
    if (c.isGroup) {
        GroupYD* g = groupOf(c);
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

bool SessionYD::send(const ConversationYD& c, const std::string& text, std::string& msg) {
    if (!c.isGroup) return platform_.sendPrivate(*current_, c.peerId, text, msg);
    GroupYD* g = groupOf(c);
    if (!g) {
        msg = "该群已解散。";
        return false;
    }
    return platform_.sendGroupMessage(*current_, *g, text, msg);
}

void SessionYD::markRead(const ConversationYD& c, int seq) {
    if (seq <= 0) return;
    MessageCenterYD& center = platform_.messages();
    if (c.isGroup) center.markGroupRead(current_->type(), c.groupNo, current_->id(), seq);
    else center.markPrivateRead(current_->type(), current_->id(), c.peerId, seq);
}

std::string SessionYD::titleOf(const ConversationYD& c) const {
    ConversationYD copy = c;
    fill(copy);
    return copy.title;
}

GroupYD* SessionYD::groupOf(const ConversationYD& c) const {
    return c.isGroup ? platform_.groups().find(current_->type(), c.groupNo) : nullptr;
}

bool SessionYD::messagesChanged() {
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
