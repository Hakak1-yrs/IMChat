#include "LocalSession.h"
#include <algorithm>
#include <map>
#include "GuiText.h"
#include "Util.h"

LocalSession::LocalSession(Platform& platform) : platform_(platform) {}

bool LocalSession::login(ServiceType type, const std::string& id, const std::string& password, std::string& msg) {
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

void LocalSession::logout() {
    if (user_) user_->logoutAll();
    user_ = nullptr;
    current_ = nullptr;
}

bool LocalSession::switchService(ServiceType type) {
    ServiceBase* target = user_ ? user_->service(type) : nullptr;
    if (!target) return false;
    target->login();
    current_ = target;
    return true;
}

bool LocalSession::closeCurrentService(std::string& msg) {
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

void LocalSession::deleteCurrentUser(std::string& msg) {
    const int id = user_->id();
    user_ = nullptr;   // 用户对象即将被删除，先清空，之后不再访问
    current_ = nullptr;
    platform_.deleteUser(id, msg);
}

bool LocalSession::isRemote() const { return false; }

bool LocalSession::loggedIn() const { return user_ != nullptr; }

User* LocalSession::user() const { return user_; }

ServiceBase* LocalSession::current() const { return current_; }

Platform& LocalSession::platform() { return platform_; }

AccountView LocalSession::me() const {
    AccountView view;
    if (!current_) return view;
    view.type = current_->type();
    view.id = current_->id();
    view.nickname = current_->nickname();
    view.location = current_->location();
    view.momentsName = current_->momentsName();
    return view;
}

std::vector<ServiceType> LocalSession::services() const {
    std::vector<ServiceType> result;
    if (user_) {
        for (ServiceBase* s : user_->services()) result.push_back(s->type());
    }
    return result;
}

int LocalSession::unreadOf(ServiceType type) const {
    ServiceBase* account = user_ ? user_->service(type) : nullptr;
    if (!account) return 0;
    int privateCount = 0, groupCount = 0;
    platform_.unreadCount(*account, privateCount, groupCount);
    return privateCount + groupCount;
}

void LocalSession::fill(Conversation& c) const {
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

std::vector<Conversation> LocalSession::recentChats() const {
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
            entry.preview = nicknameOf(m.sender()) + "：" + m.text();
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

std::vector<Conversation> LocalSession::contacts() const {
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

std::vector<Conversation> LocalSession::myGroups() const {
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

std::vector<Message> LocalSession::messages(const Conversation& c) const {
    MessageCenter& center = platform_.messages();
    return c.isGroup ? center.groupHistory(current_->type(), c.groupNo)
                     : center.conversation(current_->type(), current_->id(), c.peerId);
}

int LocalSession::peerReadSeq(const Conversation& c) const {
    // 已读记录的键是“会话对象 + 读者”：对方读我们之间的私信，对象是我，读者是对方
    return c.isGroup ? 0 : platform_.messages().readMark(current_->type(), "P", current_->id(), c.peerId);
}

bool LocalSession::canSend(const Conversation& c, std::string& reason) const {
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

bool LocalSession::send(const Conversation& c, const std::string& text, std::string& msg) {
    if (!c.isGroup) return platform_.sendPrivate(*current_, c.peerId, text, msg);
    Group* g = groupOf(c);
    if (!g) {
        msg = "该群已解散。";
        return false;
    }
    return platform_.sendGroupMessage(*current_, *g, text, msg);
}

void LocalSession::markRead(const Conversation& c, int seq) {
    if (seq <= 0) return;
    MessageCenter& center = platform_.messages();
    if (c.isGroup) center.markGroupRead(current_->type(), c.groupNo, current_->id(), seq);
    else center.markPrivateRead(current_->type(), current_->id(), c.peerId, seq);
}

std::string LocalSession::titleOf(const Conversation& c) const {
    Conversation copy = c;
    fill(copy);
    return copy.title;
}

Group* LocalSession::groupOf(const Conversation& c) const {
    return c.isGroup ? platform_.groups().find(current_->type(), c.groupNo) : nullptr;
}

std::optional<GroupView> LocalSession::group(const Conversation& c) const {
    const Group* g = groupOf(c);
    if (!g) return std::nullopt;
    const GroupPolicy& policy = g->policy();
    GroupView view;
    view.groupNo = g->groupNo();
    view.name = g->name();
    view.modeName = policy.modeName();
    view.notice = g->notice();
    view.canMentionAll = policy.canEditInfo(*g, current_->id());   // 群主（QQ 群模式下还有管理员）可以 @所有人
    for (const std::string& id : g->members()) {
        MemberView member;
        member.id = id;
        member.nickname = nicknameOf(id);
        member.owner = g->isOwner(id);
        member.admin = policy.allowAdmin() && g->isAdmin(id);
        member.muted = policy.allowMute() && g->isMuted(id);
        view.members.push_back(member);
    }
    return view;
}

ProfileView LocalSession::profile(const std::string& id) const {
    ProfileView view;
    view.id = id;
    ServiceBase* other = platform_.findAccount(current_->type(), id);
    if (!other) return view;
    view.known = true;
    view.nickname = other->nickname();
    view.location = other->location();
    view.birthday = other->birthday().toString();
    view.tAge = other->tAge();
    view.idLabel = other->idLabel();
    view.info = GuiText::capture([&] { other->showInfo(); });   // 多态：按账号的实际类型输出资料
    const Friend* f = current_->friends().find(id);
    view.isFriend = f != nullptr;
    view.remark = f && f->remark() != "-" ? f->remark() : "";
    view.peerHasMe = other->friends().contains(current_->id());
    view.blocked = platform_.isBlocked(current_->type(), current_->id(), id);
    view.mutualFriends = platform_.mutualFriends(*current_, *other);
    return view;
}

std::vector<FriendRequestView> LocalSession::friendRequests() const {
    std::vector<FriendRequestView> result;
    for (const FriendRequest& r : platform_.friendRequests(*current_)) {
        FriendRequestView view;
        view.seq = r.seq;
        view.from = r.from;
        view.to = r.to;
        view.greeting = r.greeting;
        view.time = r.time;
        view.status = r.status == FriendRequest::ACCEPTED   ? FriendRequestView::ACCEPTED
                      : r.status == FriendRequest::REJECTED ? FriendRequestView::REJECTED
                                                            : FriendRequestView::PENDING;
        result.push_back(view);
    }
    return result;
}

bool LocalSession::requiresVerification() const { return platform_.requiresVerification(*current_); }

std::vector<std::string> LocalSession::blockedList() const { return platform_.blockedList(*current_); }

void LocalSession::requestFriend(const std::string& target, const std::string& greeting, const std::string& remark, Done done) {
    std::string msg;
    const bool ok = platform_.requestFriend(*current_, target, greeting, remark, msg);
    done(ok, msg);
}

void LocalSession::respondRequest(int seq, bool accept, const std::string& remark, Done done) {
    std::string msg;
    const bool ok = platform_.respondRequest(*current_, seq, accept, remark, msg);
    done(ok, msg);
}

void LocalSession::removeFriend(const std::string& id, Done done) {
    std::string msg;
    const bool ok = platform_.removeFriend(*current_, id, msg);
    done(ok, msg);
}

void LocalSession::setRemark(const std::string& id, const std::string& remark, Done done) {
    const std::string text = Util::toField(Util::lineText(remark));
    if (text.empty()) {
        done(false, "备注不能为空。");
        return;
    }
    const bool ok = current_->friends().modifyRemark(id, text);
    done(ok, ok ? "备注已修改" : id + " 不是你的好友。");
}

void LocalSession::setBlocked(const std::string& id, bool on, Done done) {
    std::string msg;
    const bool ok = platform_.setBlocked(*current_, id, on, msg);
    done(ok, msg);
}

void LocalSession::setVerification(bool on, Done done) {
    platform_.setVerification(*current_, on);
    done(true, on ? "已开启好友验证" : "已关闭好友验证");
}

std::vector<GroupBrief> LocalSession::allGroups() const { return GroupService::list(platform_, *current_); }

std::optional<GroupInfo> LocalSession::groupInfo(int groupNo) const { return GroupService::describe(platform_, *current_, groupNo); }

void LocalSession::createGroup(const std::string& name, Created done) {
    std::string msg;
    const int groupNo = GroupService::create(platform_, *current_, name, msg);
    done(groupNo != 0, msg, groupNo);
}

void LocalSession::groupCommand(const GroupCommand& command, Done done) {
    std::string msg;
    const bool ok = GroupService::execute(platform_, *current_, command, msg);
    done(ok, msg);
}

std::string LocalSession::nicknameOf(const std::string& id) const {
    ServiceBase* account = platform_.findAccount(current_->type(), id);
    return account ? account->nickname() : id;
}

bool LocalSession::mentionsMe(const Message& m) const {
    if (!m.isGroup()) return false;
    const Group* g = platform_.groups().find(current_->type(), std::atoi(m.target().c_str()));
    return g && Util::contains(platform_.mentionsOf(*g, m), current_->id());
}

bool LocalSession::canRevoke(const Message& m) const { return platform_.canRevoke(*current_, m); }

void LocalSession::revoke(const Message& m) {
    std::string msg;
    if (!platform_.revokeMessage(*current_, m.seq(), msg)) notice_ = msg;
}

std::string LocalSession::revokeText(const Message& m) const { return platform_.revokeText(*current_, m); }

bool LocalSession::changed() {
    // 检查消息文件：本窗口、其他窗口或控制台程序写入新记录后，大小或修改时间会变
    std::error_code ec;
    const std::filesystem::path file = Util::pathOf(platform_.dataDir() + "messages.txt");
    auto stamp = std::filesystem::last_write_time(file, ec);
    if (ec) stamp = {};
    std::uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec) size = 0;
    if (stamp == stamp_ && size == size_) return false;
    stamp_ = stamp;
    size_ = size;
    platform_.syncSocial();   // 其他窗口刚写入的好友申请结果、删除、拉黑
    return true;
}

std::string LocalSession::takeNotice() {
    std::string notice;
    notice.swap(notice_);
    return notice;
}

int LocalSession::pendingFriendRequests() const { return current_ ? platform_.pendingRequestCount(*current_) : 0; }

int LocalSession::momentNotices() const {
    return current_ ? static_cast<int>(platform_.momentNotices(*current_, true).size()) : 0;
}
