#include "Menu.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include "GroupChatRoom.h"
#include "IndependentIdService.h"
#include "TcpChat.h"
#include "Util.h"

using std::cout;
using std::string;

Menu::Menu(Platform& platform)
    : platform_(platform), user_(nullptr), current_(nullptr), chatLog_(platform.dataDir() + "chat_log.txt") {}

void Menu::run() {
    while (true) {
        cout << "\n==================================================\n"
             << "            IMChat 即时通信系统（控制台客户端）\n"
             << "==================================================\n"
             << "  1. 登录\n  2. 注册新用户\n  3. 平台概况\n  4. 立即保存数据\n  0. 退出\n";
        switch (Util::readInt("请选择：", 0, 4)) {
        case 1:
            login();
            if (user_) userMenu();
            break;
        case 2: registerUser(); break;
        case 3: showOverview(); break;
        case 4: cout << (platform_.save() ? "数据已保存。\n" : "保存失败！\n"); break;
        case 0: cout << "再见！\n"; return;
        }
    }
}

// ======================== 主菜单 ========================

void Menu::login() {
    cout << "\n---- 登录 ----\n";
    ServiceType type = askServiceType("选择登录的服务：");
    string id = Util::readWord("账号：");
    string password = Util::readWord("密码：");
    ServiceBase* account = platform_.findAccount(type, id);
    User* owner = platform_.ownerOf(account);
    if (!account || !owner || !account->checkPassword(password)) {
        cout << "账号或密码错误！\n";
        return;
    }
    account->login();
    user_ = owner;
    current_ = account;
    cout << "登录成功！欢迎你，" << account->nickname() << "。\n";

    // 登录联动：只要一个服务登录，其它已开通的服务简单确认后即视为登录
    for (ServiceBase* s : user_->services()) {
        if (s == account) continue;
        if (Util::confirm("检测到你已开通" + s->serviceName() + " " + s->displayName() + "，是否同步登录？(y/n)：")) {
            s->login();
            cout << "  " << s->serviceName() << " 已自动登录。\n";
        }
    }
    showUnreadSummary();
}

void Menu::registerUser() {
    cout << "\n---- 注册新用户 ----\n";
    string name = Util::readWord("真实姓名：");
    cout << "选择首个开通的服务：\n";
    ServiceType type = askServiceType("请选择：");
    string msg;
    if (!platform_.canRegisterWith(type, msg)) {   // 先检查，避免填完资料才提示失败
        cout << msg << "\n";
        return;
    }
    string nickname, location, password;
    Date birthday;
    askAccountInfo(nickname, birthday, location, password);
    platform_.registerUser(name, type, nickname, birthday, location, password, msg);
    cout << msg << "\n";
}

void Menu::showOverview() const {
    cout << "\n---- 平台概况 ----\n";
    for (const auto& kv : platform_.users()) {
        const User& u = kv.second;
        cout << "  用户" << u.id() << "  " << u.name() << "：";
        for (ServiceBase* s : u.services()) cout << s->serviceName() << "[" << s->id() << "]  ";
        cout << "\n";
    }
    cout << "  群数量：";
    for (ServiceType t : ServiceTypeHelper::all())
        cout << ServiceTypeHelper::name(t) << " " << platform_.groups().groupsOfService(t).size() << " 个  ";
    cout << "\n  （预置演示账号的密码均为 123456）\n";
}

// ======================== 用户菜单 ========================

void Menu::userMenu() {
    while (user_) {
        printStatus();
        platform_.syncSocial();   // 其他窗口可能刚通过了我的好友申请、删除或拉黑了我
        const int requests = platform_.pendingRequestCount(*current_);
        const size_t notices = platform_.momentNotices(*current_, true).size();
        cout << "  1. 切换当前服务\n  2. 开通新服务\n  3. 个人信息\n  4. 好友管理"
             << (requests ? "  【" + std::to_string(requests) + " 个新的好友申请】" : "") << "\n  5. 群管理\n"
             << "  6. QQ点对点聊天（TCP）\n  7. 消息中心（好友私信、群聊天）" << unreadLabel() << "\n  8. "
             << current_->momentsName() << (notices ? "  【" + std::to_string(notices) + " 条新互动】" : "") << "\n  0. 注销登录\n";
        switch (Util::readInt("请选择：", 0, 8)) {
        case 8: momentsMenu(); break;
        case 1: switchService(); break;
        case 2: openService(); break;
        case 3: showProfile(); break;
        case 4: friendMenu(); break;
        case 5: groupMenu(); break;
        case 6: tcpChat(); break;
        case 7: messageCenter(); break;
        case 0:
            user_->logoutAll();
            user_ = nullptr;
            current_ = nullptr;
            cout << "已注销全部服务。\n";
            break;
        }
    }
}

void Menu::printStatus() const {
    cout << "\n============ 用户：" << user_->name() << "（编号 " << user_->id() << "）============\n"
         << "当前服务：" << current_->serviceName() << "  " << current_->displayName() << "\n服务状态：";
    for (ServiceType t : ServiceTypeHelper::all()) {
        ServiceBase* s = user_->service(t);
        cout << ServiceTypeHelper::name(t) << (s == nullptr ? "[未开通]  " : (s->isLoggedIn() ? "[在线]  " : "[已开通]  "));
    }
    cout << "\n";
}

void Menu::switchService() {
    std::vector<ServiceBase*> list = user_->services();
    cout << "\n---- 切换当前服务 ----\n";
    for (size_t i = 0; i < list.size(); ++i) {
        cout << "  " << i + 1 << ". " << list[i]->serviceName() << "  " << list[i]->displayName()
             << (list[i]->isLoggedIn() ? "  [在线]" : "  [未登录]") << "\n";
    }
    cout << "  0. 返回\n";
    int choice = Util::readInt("请选择：", 0, static_cast<int>(list.size()));
    if (choice == 0) return;
    ServiceBase* target = list[choice - 1];
    if (!target->isLoggedIn()) {
        if (!Util::confirm("你已有服务在线，确认直接登录" + target->serviceName() + "？(y/n)：")) return;
        target->login();
    }
    current_ = target;
    cout << "已切换到 " << target->serviceName() << "。\n";
}

void Menu::openService() {
    std::vector<ServiceType> options;
    for (ServiceType t : ServiceTypeHelper::all()) {
        if (!user_->hasService(t)) options.push_back(t);
    }
    cout << "\n---- 开通新服务 ----\n";
    if (options.empty()) {
        cout << "你已开通平台上的全部服务。\n";
        return;
    }
    for (size_t i = 0; i < options.size(); ++i)
        cout << "  " << i + 1 << ". " << ServiceTypeHelper::name(options[i]) << "\n";
    cout << "  0. 返回\n";
    int choice = Util::readInt("请选择：", 0, static_cast<int>(options.size()));
    if (choice == 0) return;

    string nickname, location, password;
    Date birthday;
    askAccountInfo(nickname, birthday, location, password);
    string msg;
    ServiceBase* account = platform_.openService(*user_, options[choice - 1], nickname, birthday, location, password, msg);
    cout << msg << "\n";
    if (!account) return;
    account->login();   // 已有服务在线，新开通的服务直接进入登录状态

    // 微信、微商等独立号码的服务：绑定 QQ 号后才能与 QQ、微博互认好友
    auto* independent = dynamic_cast<IndependentIdService*>(account);
    ServiceBase* qq = user_->service(ServiceType::QQ);
    if (independent && qq &&
        Util::confirm("是否将" + account->serviceName() + "与你的QQ号 " + qq->id() + " 绑定？(y/n)：")) {
        platform_.bindQQ(*user_, *independent, msg);
        cout << msg << "\n";
    }
}

void Menu::showProfile() {
    while (true) {
        cout << "\n---- 个人信息 ----\n";
        current_->showInfo();   // 多态：不同服务显示各自的特有信息
        cout << "  好友列表：";
        if (current_->friends().size() == 0) cout << "无";
        for (const Friend& f : current_->friends().all()) cout << platform_.displayName(current_->type(), f.id()) << "  ";
        cout << "\n  群列表：";
        std::vector<Group*> groups = platform_.groups().groupsOf(current_->type(), current_->id());
        if (groups.empty()) cout << "无";
        for (Group* g : groups) cout << g->groupNo() << "(" << g->name() << ")  ";
        auto* independent = dynamic_cast<IndependentIdService*>(current_);
        cout << "\n\n  1. 修改昵称\n  2. 修改所在地\n  3. 修改出生日期\n  4. 修改密码\n"
             << "  5. 绑定/解绑QQ" << (independent ? "" : "（仅微信、微商等独立号码的服务可用）") << "\n"
             << "  6. 关闭本服务（" << current_->serviceName() << "）\n  7. 注销整个账号\n  0. 返回\n";
        switch (Util::readInt("请选择：", 0, 7)) {
        case 0: return;
        case 1: current_->setNickname(Util::readWord("新昵称：")); break;
        case 2: current_->setLocation(Util::readWord("新所在地：")); break;
        case 3: current_->setBirthday(askBirthday()); cout << "出生日期已修改。\n"; break;
        case 4: changePassword(); break;
        case 5:
            if (independent) bindQQ(*independent);
            else cout << current_->serviceName() << "与QQ共享号码，本来就对应你的QQ号，不需要绑定。\n";
            break;
        case 6:
            if (closeCurrentService()) return;
            break;
        case 7:
            if (deleteAccount()) return;
            break;
        }
    }
}

// 修改密码前先验证旧密码；各服务账号的密码相互独立
void Menu::changePassword() {
    if (!current_->checkPassword(Util::readWord("请输入原密码："))) {
        cout << "原密码错误，未修改。\n";
        return;
    }
    current_->setPassword(askNewPassword());
    cout << current_->serviceName() << " 密码已修改（只保存加密后的记录）。\n";
}

bool Menu::closeCurrentService() {
    if (user_->services().size() == 1) {
        cout << "这是你唯一开通的服务，关闭后将无法登录。如需离开平台，请选择“注销整个账号”。\n";
        return false;
    }
    cout << "关闭后将删除该服务的账号、好友关系和群成员身份（群主身份会自动移交），无法恢复。\n";
    if (!Util::confirm("确认关闭" + current_->serviceName() + "？(y/n)：")) return false;
    if (!current_->checkPassword(Util::readWord("请输入该服务的密码确认："))) {
        cout << "密码错误，已取消。\n";
        return false;
    }
    string msg;
    platform_.closeService(*user_, current_->type(), msg);
    cout << msg << "\n";
    // 切换到仍在线的其他服务
    current_ = nullptr;
    for (ServiceBase* s : user_->services()) {
        if (!current_ || s->isLoggedIn()) current_ = s;
    }
    current_->login();
    cout << "当前服务已切换为 " << current_->serviceName() << "。\n";
    return true;
}

bool Menu::deleteAccount() {
    cout << "注销将删除你在平台上的全部服务账号、好友关系和群成员身份，无法恢复。\n";
    if (!Util::confirm("确认注销整个账号？(y/n)：")) return false;
    if (!current_->checkPassword(Util::readWord("请输入当前服务的密码确认："))) {
        cout << "密码错误，已取消。\n";
        return false;
    }
    string msg;
    platform_.deleteUser(user_->id(), msg);
    cout << msg << "\n";
    user_ = nullptr;
    current_ = nullptr;
    return true;
}

void Menu::bindQQ(IndependentIdService& account) {
    string msg;
    if (account.isBound()) {
        cout << "解绑后，" << account.serviceName() << "将不能再与QQ、微博互认好友（共同好友、推荐好友）。\n";
        if (Util::confirm("当前已绑定QQ " + account.boundQQ() + "，是否解绑？(y/n)：")) {
            platform_.unbindQQ(account, msg);
            cout << msg << "\n";
        }
        return;
    }
    platform_.bindQQ(*user_, account, msg);
    cout << msg << "\n";
}

void Menu::tcpChat() {
    if (current_->type() != ServiceType::QQ) {
        cout << "点对点聊天是QQ的功能，请先切换到QQ服务。\n";
        return;
    }
    cout << "\n---- QQ点对点聊天（TCP）----\n"
         << "提示：本机演示时可再打开一个程序窗口，一个选 1，另一个选 2，IP 填 127.0.0.1。\n"
         << "  1. 等待好友连接（服务端）\n  2. 连接好友（客户端）\n  3. 查看聊天记录\n  0. 返回\n";
    int choice = Util::readInt("请选择：", 0, 3);
    if (choice == 0) return;
    if (choice == 3) {
        string keyword = Util::readLine("查询关键字（直接回车显示最近 20 条）：");
        int shown = chatLog_.show(keyword, 20);
        cout << (shown == 0 ? "没有找到聊天记录。\n" : "共显示 " + std::to_string(shown) + " 条。\n");
        return;
    }
    int port = Util::readInt("端口号（1024-65535，如 8888）：", 1024, 65535);
    TcpChat chat(current_->nickname(), chatLog_);
    if (choice == 1) {
        chat.runServer(static_cast<unsigned short>(port));
    } else {
        string ip = Util::readWord("对方IP地址（本机为 127.0.0.1）：");
        chat.runClient(ip, static_cast<unsigned short>(port));
    }
}

// ======================== 消息中心 ========================

void Menu::showUnreadSummary() const {
    bool any = false;
    for (ServiceBase* s : user_->services()) {
        if (!s->isLoggedIn()) continue;
        int privateCount = 0, groupCount = 0;
        platform_.unreadCount(*s, privateCount, groupCount);
        const int requests = platform_.pendingRequestCount(*s);
        const size_t notices = platform_.momentNotices(*s, true).size();
        if (privateCount + groupCount + requests + notices == 0) continue;
        if (!any) cout << "【新消息】\n";
        any = true;
        cout << "  " << s->serviceName() << "：未读私信 " << privateCount << " 条，未读群消息 " << groupCount << " 条";
        if (requests) cout << "，新的好友申请 " << requests << " 个";
        if (notices) cout << "，" << s->momentsName() << "新互动 " << notices << " 条";
        cout << "\n";
    }
    if (any) cout << "  请在对应的菜单中查看（切换到对应的服务）。\n";
}

string Menu::unreadLabel() const {
    int privateCount = 0, groupCount = 0;
    platform_.unreadCount(*current_, privateCount, groupCount);
    return privateCount + groupCount == 0 ? "" : "  【" + std::to_string(privateCount + groupCount) + " 条未读】";
}

void Menu::messageCenter() {
    while (true) {
        int privateCount = 0, groupCount = 0;
        platform_.unreadCount(*current_, privateCount, groupCount);
        cout << "\n---- 消息中心（" << current_->serviceName() << " " << current_->displayName() << "）----\n"
             << "未读私信 " << privateCount << " 条，未读群消息 " << groupCount << " 条\n"
             << "  1. 好友私信（可给不在线的好友留言）\n  2. 群聊天（群聊记录、发消息、实时群聊室）\n  0. 返回\n";
        switch (Util::readInt("请选择：", 0, 2)) {
        case 1: choosePrivateChat(); break;
        case 2: chooseGroupChat(); break;
        case 0: return;
        }
    }
}

void Menu::choosePrivateChat() {
    std::map<string, int> unread = platform_.messages().unreadPrivate(current_->type(), current_->id());
    std::vector<string> peers;
    for (const Friend& f : current_->friends().all()) peers.push_back(f.id());
    for (const auto& kv : unread) {   // 已经删除好友、但对方之前发来的消息还没看过
        if (!current_->friends().contains(kv.first)) peers.push_back(kv.first);
    }
    if (peers.empty()) {
        cout << "你还没有" << current_->serviceName() << "好友，先在“好友管理”中添加好友吧。\n";
        return;
    }
    for (size_t i = 0; i < peers.size(); ++i) {
        cout << "  " << i + 1 << ". " << platform_.displayName(current_->type(), peers[i]);
        if (unread.count(peers[i])) cout << "  【" << unread[peers[i]] << " 条未读】";
        if (!current_->friends().contains(peers[i])) cout << "  （已不是好友，只能查看）";
        cout << "\n";
    }
    cout << "  0. 返回\n";
    int choice = Util::readInt("选择聊天对象：", 0, static_cast<int>(peers.size()));
    if (choice > 0) privateChat(peers[choice - 1]);
}

void Menu::privateChat(const string& friendId) {
    const ServiceType type = current_->type();
    const string me = current_->id();
    MessageStore& messages = platform_.messages();
    chatWindow("与 " + platform_.displayName(type, friendId) + " 的私信", false,
               [&] { return messages.conversation(type, me, friendId); },
               [&](const string& text, string& msg) { return platform_.sendPrivate(*current_, friendId, text, msg); },
               [&](int seq) { messages.markPrivateRead(type, me, friendId, seq); });
}

void Menu::chooseGroupChat() {
    std::vector<Group*> groups = platform_.groups().groupsOf(current_->type(), current_->id());
    if (groups.empty()) {
        cout << "你还没有加入任何" << current_->serviceName() << "群。\n";
        return;
    }
    std::vector<int> groupNos;
    for (Group* g : groups) groupNos.push_back(g->groupNo());
    std::map<int, int> unread = platform_.messages().unreadGroups(current_->type(), current_->id(), groupNos);
    for (size_t i = 0; i < groups.size(); ++i) {
        cout << "  " << i + 1 << ". " << groups[i]->groupNo() << "  " << groups[i]->name();
        if (platform_.unreadMentions(*current_, groups[i]->groupNo()) > 0) cout << "  【有人@我】";
        if (unread.count(groups[i]->groupNo())) cout << "  【" << unread[groups[i]->groupNo()] << " 条未读】";
        cout << "\n";
    }
    cout << "  0. 返回\n";
    int choice = Util::readInt("选择群：", 0, static_cast<int>(groups.size()));
    if (choice > 0) groupChat(*groups[choice - 1]);
}

void Menu::groupChat(Group& group) {
    const string me = current_->id();
    if (!group.isMember(me)) {
        cout << "只有群成员才能查看和发送群消息。\n";
        return;
    }
    const ServiceType type = group.service();
    const int groupNo = group.groupNo();
    MessageStore& messages = platform_.messages();
    while (true) {
        cout << "\n---- 群聊天：" << groupNo << " " << group.name() << "（" << group.policy().modeName() << "，"
             << group.members().size() << " 人）----\n";
        if (!group.notice().empty()) cout << "群公告：" << group.notice() << "\n";
        if (!group.canSpeak(me)) cout << "（你已被禁言，只能查看消息）\n";
        cout << "  1. 进入聊天窗口（查看最近消息、发送群消息）\n  2. 按关键字查找群聊记录\n"
             << "  3. 开设实时群聊室（TCP 服务端，多位成员同时在线聊天）\n  4. 加入实时群聊室（TCP 客户端）\n  0. 返回\n";
        switch (Util::readInt("请选择：", 0, 4)) {
        case 0: return;
        case 1:
            chatWindow("群 " + std::to_string(groupNo) + "（" + group.name() + "）", true,
                       [&] { return messages.groupHistory(type, groupNo); },
                       [&](const string& text, string& msg) { return platform_.sendGroupMessage(*current_, group, text, msg); },
                       [&](int seq) { messages.markGroupRead(type, groupNo, me, seq); });
            break;
        case 2: searchGroupHistory(group); break;
        case 3: groupChatRoom(group, true); break;
        case 4: groupChatRoom(group, false); break;
        }
    }
}

void Menu::searchGroupHistory(const Group& group) const {
    string keyword = Util::readLine("查询关键字（消息内容、发送者昵称或账号；直接回车显示全部）：");
    int found = 0;
    for (const Message& m : platform_.messages().groupHistory(group.service(), group.groupNo())) {
        if (!Util::containsText(m.text(), keyword) && !Util::containsText(m.sender(), keyword) &&
            !Util::containsText(platform_.displayName(m.service(), m.sender()), keyword))
            continue;
        printMessage(m, 0);
        ++found;
    }
    cout << (found == 0 ? "没有找到群聊记录。\n" : "共找到 " + std::to_string(found) + " 条。\n");
}

void Menu::groupChatRoom(Group& group, bool host) {
    cout << "提示：本机演示时再打开一个或几个程序窗口，登录本群的其他成员；一个窗口开设群聊室，其他窗口加入，IP 填 127.0.0.1。\n";
    int port = Util::readInt("端口号（1024-65535，如 9999）：", 1024, 65535);
    GroupChatRoom room(platform_, group, *current_);
    if (host) {
        room.host(static_cast<unsigned short>(port));
    } else {
        string ip = Util::readWord("群聊室所在电脑的 IP（本机为 127.0.0.1）：");
        if (!room.join(ip, static_cast<unsigned short>(port))) return;
    }
    // 群聊室里的消息已经实时看过了，记为已读（消息由开设群聊室的一方写入群聊记录）
    std::vector<Message> history = platform_.messages().groupHistory(group.service(), group.groupNo());
    if (!history.empty())
        platform_.messages().markGroupRead(group.service(), group.groupNo(), current_->id(), history.back().seq());
}

void Menu::chatWindow(const string& title, bool group, const std::function<std::vector<Message>()>& load,
                        const std::function<bool(const string&, string&)>& send,
                        const std::function<void(int)>& markRead) {
    const size_t recent = 15;
    cout << "\n---- " << title << " ----\n";
    std::vector<Message> list = load();
    if (list.empty()) cout << "  （还没有消息）\n";
    if (list.size() > recent) cout << "  ……更早的 " << list.size() - recent << " 条消息可以用关键字查找\n";
    // 每条消息前的编号就是它在会话中的位置，撤回时用来指定是哪一条
    for (size_t i = list.size() > recent ? list.size() - recent : 0; i < list.size(); ++i) printMessage(list[i], i + 1);
    int lastSeq = list.empty() ? 0 : list.back().seq();
    markRead(lastSeq);
    cout << "输入消息后回车发送；直接回车刷新（查看对方刚发来的消息）；/w 撤回自己刚发的消息（2 分钟内）";
    if (group) cout << "；/w 编号 撤回指定的消息（群主、管理员可以撤回成员的消息）；“@昵称”提醒某人，群主可以“@所有人”";
    cout << "；输入 /q 返回。\n";
    while (true) {
        string line = Util::readLine("> ");
        if (line == "/q" || line == "/quit") break;
        string msg;
        if (line == "/w" || line.compare(0, 3, "/w ") == 0) {
            int seq = 0;
            if (line == "/w") {   // 撤回自己最近发的一条
                for (auto it = list.rbegin(); it != list.rend() && !seq; ++it) {
                    if (it->sender() == current_->id() && !it->revoked()) seq = it->seq();
                }
            } else {
                const int number = std::atoi(line.c_str() + 3);
                if (number >= 1 && number <= static_cast<int>(list.size())) seq = list[number - 1].seq();
            }
            if (!seq) cout << "  没有找到要撤回的消息。\n";
            else cout << "  " << (platform_.revokeMessage(*current_, seq, msg) ? "已撤回。" : msg) << "\n";
        } else if (!line.empty() && !send(line, msg)) {
            cout << "  [未发送] " << msg << "\n";
        }
        // 显示上次之后的新消息：自己刚发的，以及别的窗口发来的
        list = load();
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].seq() <= lastSeq) continue;
            printMessage(list[i], i + 1);
            lastSeq = list[i].seq();
        }
        markRead(lastSeq);
    }
}

void Menu::printMessage(const Message& m, size_t number) const {
    const string time = m.time().size() >= 16 ? m.time().substr(5, 11) : m.time();   // 只显示“月-日 时:分”
    if (m.isSystem()) {   // 群里的系统提示：A 邀请 B 加入了群聊……
        cout << "        —— " << m.text() << " ——\n";
        return;
    }
    const string index = number ? std::to_string(number) + ") " : "";
    if (m.revoked()) {
        cout << "  " << index << "[" << time << "] " << platform_.revokeText(*current_, m) << "\n";
        return;
    }
    const string who = m.sender() == current_->id() ? "我" : platform_.displayName(m.service(), m.sender());
    cout << "  " << index << "[" << time << "] " << who << "：" << m.text();
    if (m.isGroup()) {
        const Group* g = platform_.groups().find(m.service(), std::atoi(m.target().c_str()));
        if (g && Util::contains(platform_.mentionsOf(*g, m), current_->id())) cout << "  【@我】";
    }
    cout << "\n";
}

// ======================== 朋友圈 ========================

void Menu::momentsMenu() {
    while (true) {
        const string name = current_->momentsName();   // 多态：QQ 空间、朋友圈、微博动态……
        std::vector<string> notices = platform_.momentNotices(*current_, true);
        cout << "\n---- " << name << "（" << current_->serviceName() << " " << current_->displayName() << "）----\n";
        if (!notices.empty()) {
            cout << "【" << notices.size() << " 条新互动】\n";
            for (const string& n : notices) cout << "  · " << n << "\n";
            platform_.markMomentsRead(*current_);
        }
        cout << "  1. 浏览" << name << "\n  2. 发表动态\n  3. 点赞 / 取消点赞\n  4. 评论 / 回复评论\n"
             << "  5. 只看某个人的动态\n  6. 删除我的动态\n  7. 删除评论\n  8. 全部互动消息\n  0. 返回\n";
        string msg;
        switch (Util::readInt("请选择：", 0, 8)) {
        case 0: return;
        case 1: showMoments(platform_.moments(*current_)); break;
        case 2: postMoment(); break;
        case 3:
            platform_.toggleLike(*current_, askMoment("要点赞（或取消赞）的动态编号："), msg);
            cout << msg << "\n";
            break;
        case 4: {
            const int seq = askMoment("要评论的动态编号：");
            string replyTo = Util::trim(Util::readLine("回复某条评论的作者账号（直接回车表示评论动态）："));
            platform_.commentMoment(*current_, seq, replyTo, Util::readLine("评论内容："), msg);
            cout << msg << "\n";
            break;
        }
        case 5: showMoments(platform_.moments(*current_, Util::readWord("对方账号："))); break;
        case 6:
            platform_.deleteMoment(*current_, askMoment("要删除的动态编号："), msg);
            cout << msg << "\n";
            break;
        case 7:
            platform_.deleteComment(*current_, askMoment("要删除的评论编号（评论前方括号中的数字）："), msg);
            cout << msg << "\n";
            break;
        case 8: {
            std::vector<string> all = platform_.momentNotices(*current_, false);
            if (all.empty()) cout << "  暂无互动消息。\n";
            for (const string& n : all) cout << "  · " << n << "\n";
            break;
        }
        }
    }
}

void Menu::showMoments(const std::vector<Moment>& list) const {
    if (list.empty()) {
        cout << "  还没有动态（只能看到互为好友的人的动态）。\n";
        return;
    }
    const ServiceType type = current_->type();
    for (const Moment& m : list) {
        cout << "\n  [动态 " << m.seq << "] " << platform_.displayName(type, m.author) << "  " << m.time.substr(0, 16)
             << (m.author == current_->id() ? "  （" + Moment::scopeText(m.scope) + "）" : "") << "\n"
             << "      " << m.text << "\n";
        if (!m.likes.empty()) {
            cout << "      赞：";
            for (size_t i = 0; i < m.likes.size(); ++i) cout << (i ? "、" : "") << platform_.displayName(type, m.likes[i]);
            cout << "\n";
        }
        for (const MomentComment& c : m.comments) {
            cout << "      [" << c.seq << "] " << platform_.displayName(type, c.author);
            if (c.isReply()) cout << " 回复 " << platform_.displayName(type, c.replyTo);
            cout << "：" << c.text << "\n";
        }
    }
    cout << "  （只显示共同好友的点赞和评论）\n";
}

void Menu::postMoment() {
    string text = Util::readLine("动态内容：");
    int choice = Util::readInt("谁可以看：1. 公开（所有好友）  2. 私密（仅自己）  3. 部分可见  4. 不给谁看：", 1, 4);
    string scope = choice == 1 ? "all" : "self";
    if (choice >= 3) {
        listFriends();
        std::vector<string> ids = Util::split(Util::readLine("好友账号（多个用英文逗号分隔）："), ',');
        scope = (choice == 3 ? "only:" : "except:") + Util::joinList(ids);
    }
    string msg;
    platform_.postMoment(*current_, text, scope, msg);
    cout << msg << "\n";
}

int Menu::askMoment(const string& prompt) const { return Util::readInt(prompt, 1, 99999999); }

// ======================== 好友管理 ========================

void Menu::friendMenu() {
    while (true) {
        const int requests = platform_.pendingRequestCount(*current_);
        const bool verify = platform_.requiresVerification(*current_);
        cout << "\n---- 好友管理（" << current_->serviceName() << "）----\n"
             << "  1. 查看好友列表\n  2. 添加好友（发送好友申请）\n  3. 修改好友备注\n  4. 删除好友\n  5. 查询好友\n"
             << "  6. 从其他服务的好友中添加（推荐好友）\n  7. 查询与其他服务的共同好友\n"
             << "  8. 查看好友资料（含我们的共同好友）\n  9. 新的朋友（好友申请）"
             << (requests ? "  【" + std::to_string(requests) + " 个待处理】" : "") << "\n 10. 黑名单\n"
             << " 11. 加我为好友时需要验证（当前：" << (verify ? "开启" : "关闭") << "）\n  0. 返回\n";
        switch (Util::readInt("请选择：", 0, 11)) {
        case 9: newFriends(); break;
        case 10: blacklistMenu(); break;
        case 11:
            platform_.setVerification(*current_, !verify);
            cout << (verify ? "已关闭好友验证：别人添加你时直接成为好友。\n" : "已开启好友验证：别人添加你时需要你同意。\n");
            break;
        case 8: showFriendDetail(); break;
        case 1: listFriends(); break;
        case 2: addFriend(); break;
        case 3: modifyFriend(); break;
        case 4: deleteFriend(); break;
        case 5: searchFriend(); break;
        case 6: addFromOtherService(); break;
        case 7: showCommonFriends(); break;
        case 0: return;
        }
    }
}

void Menu::listFriends() const {
    const std::vector<Friend>& list = current_->friends().all();
    cout << "共 " << list.size() << " 位好友：\n";
    for (const Friend& f : list)
        cout << "  " << platform_.displayName(current_->type(), f.id()) << "  备注：" << f.remark() << "\n";
}

void Menu::addFriend() {
    string id = Util::readWord("对方账号：");
    string greeting = Util::readLine("验证消息（直接回车则为“我是" + current_->nickname() + "”）：");
    string remark = Util::toField(Util::readLine("备注（直接回车则使用对方昵称）："));
    string msg;
    platform_.requestFriend(*current_, id, greeting, remark, msg);
    cout << msg << "\n";
}

// 新的朋友：收到的和发出的好友申请；收到的、等待验证的可以同意或拒绝
void Menu::newFriends() {
    while (true) {
        std::vector<FriendRequest> list = platform_.friendRequests(*current_);
        cout << "\n---- 新的朋友 ----\n";
        if (list.empty()) {
            cout << "  暂无好友申请。\n";
            return;
        }
        for (size_t i = 0; i < list.size(); ++i) {
            const FriendRequest& r = list[i];
            const bool incoming = r.to == current_->id();
            cout << "  " << i + 1 << ". " << (incoming ? "[收到] " : "[发出] ")
                 << platform_.displayName(current_->type(), incoming ? r.from : r.to) << "  “" << r.greeting << "”  "
                 << r.time.substr(0, 16) << "  —— " << r.statusText() << "\n";
        }
        cout << "  0. 返回\n";
        int choice = Util::readInt("选择要处理的申请：", 0, static_cast<int>(list.size()));
        if (choice == 0) return;
        const FriendRequest& r = list[choice - 1];
        if (r.to != current_->id() || r.status != FriendRequest::PENDING) {
            cout << "只能处理别人发给你、还在等待验证的申请。\n";
            continue;
        }
        int action = Util::readInt("1. 同意  2. 拒绝  0. 返回：", 0, 2);
        if (action == 0) continue;
        string remark = action == 1 ? Util::toField(Util::readLine("给对方的备注（直接回车则使用对方昵称）：")) : "";
        string msg;
        platform_.respondRequest(*current_, r.seq, action == 1, remark, msg);
        cout << msg << "\n";
    }
}

void Menu::blacklistMenu() {
    std::vector<string> list = platform_.blockedList(*current_);
    cout << "\n---- 黑名单 ----\n";
    if (list.empty()) cout << "  黑名单为空。\n";
    for (const string& id : list) cout << "  " << platform_.displayName(current_->type(), id) << "\n";
    cout << "拉黑后不再接收对方的消息和好友申请，对方也看不到你的" << current_->momentsName() << "（好友关系保留）。\n";
    int action = Util::readInt("1. 加入黑名单  2. 移出黑名单  0. 返回：", 0, 2);
    if (action == 0) return;
    string msg;
    platform_.setBlocked(*current_, Util::readWord("对方账号："), action == 1, msg);
    cout << msg << "\n";
}

void Menu::modifyFriend() {
    string id = Util::readWord("好友账号：");
    if (!current_->friends().contains(id)) {
        cout << id << " 不是你的好友。\n";
        return;
    }
    current_->friends().modifyRemark(id, Util::readWord("新备注："));
    cout << "备注已修改。\n";
}

void Menu::deleteFriend() {
    string id = Util::readWord("要删除的好友账号：");
    cout << "与微信相同，删除是单向的：只从你的列表中删除，对方列表里仍有你。\n";
    if (!Util::confirm("确认删除 " + platform_.displayName(current_->type(), id) + "？(y/n)：")) return;
    string msg;
    platform_.removeFriend(*current_, id, msg);
    cout << msg << "\n";
}

void Menu::searchFriend() const {
    string keyword = Util::readWord("输入账号、昵称或备注中的关键字：");
    std::vector<Friend> result = platform_.searchFriends(*current_, keyword);
    cout << "找到 " << result.size() << " 位好友：\n";
    for (const Friend& f : result)
        cout << "  " << platform_.displayName(current_->type(), f.id()) << "  备注：" << f.remark() << "\n";
}

void Menu::addFromOtherService() {
    ServiceType source;
    if (!chooseOtherService(source)) return;
    string sourceName = ServiceTypeHelper::name(source);
    for (bool first = true;; first = false) {
        std::vector<ServiceBase*> list;
        std::vector<string> skipped;
        string reason;
        if (!platform_.recommendFriends(*user_, current_->type(), source, list, skipped, reason)) {
            cout << reason << "\n";
            return;
        }
        if (list.empty()) {
            if (first) {
                for (const string& s : skipped) cout << "  （未推荐）" << s << "\n";
            }
            cout << "暂无可推荐的好友（你的" << sourceName << "好友没有开通" << current_->serviceName()
                 << "、已经是你的好友，或无法确认是同一人）。\n";
            return;
        }
        cout << "根据你的" << sourceName << "好友，推荐以下" << current_->serviceName() << "用户：\n";
        for (size_t i = 0; i < list.size(); ++i) {
            User* owner = platform_.ownerOf(list[i]);
            cout << "  " << i + 1 << ". " << list[i]->displayName() << "  ← " << sourceName << "好友 "
                 << owner->service(source)->displayName() << "\n";
        }
        if (first) {
            for (const string& s : skipped) cout << "  （未推荐）" << s << "\n";
        }
        cout << "  0. 返回\n";
        int choice = Util::readInt("选择要添加的好友（发送好友申请）：", 0, static_cast<int>(list.size()));
        if (choice == 0) return;
        string msg;
        platform_.requestFriend(*current_, list[choice - 1]->id(), "", "", msg);
        cout << msg << "\n";
    }
}

void Menu::showCommonFriends() {
    ServiceType other;
    if (!chooseOtherService(other)) return;
    std::vector<User*> list;
    std::vector<string> skipped;
    string reason;
    if (!platform_.commonFriends(*user_, current_->type(), other, list, skipped, reason)) {
        cout << reason << "\n";
        return;
    }
    cout << "在" << current_->serviceName() << "和" << ServiceTypeHelper::name(other)
         << "中都是你好友的人（共 " << list.size() << " 位）：\n";
    for (User* u : list) {
        cout << "  " << u->name() << "：" << current_->serviceName() << " " << u->service(current_->type())->displayName()
             << "  /  " << ServiceTypeHelper::name(other) << " " << u->service(other)->displayName() << "\n";
    }
    for (const string& s : skipped) cout << "  （未计入）" << s << "\n";
}

void Menu::showFriendDetail() {
    string id = Util::readWord("好友账号：");
    const Friend* f = current_->friends().find(id);
    ServiceBase* other = platform_.findAccount(current_->type(), id);
    if (!f || !other) {
        cout << id << " 不是你的好友。\n";
        return;
    }
    cout << "\n---- 好友资料 ----\n";
    other->showInfo();   // 多态：按好友账号的实际类型显示
    cout << "  我给他的备注：" << f->remark() << "\n  我们的共同好友：";
    std::vector<string> mutual = platform_.mutualFriends(*current_, *other);
    if (mutual.empty()) cout << "无";
    for (const string& m : mutual) cout << platform_.displayName(current_->type(), m) << "  ";
    cout << "\n";
}

// ======================== 群管理 ========================

void Menu::groupMenu() {
    while (true) {
        cout << "\n---- 群管理（" << current_->serviceName() << "）----\n"
             << "  1. 查看本服务所有群\n  2. 查看群详情与成员\n  3. 申请加入群\n  4. 推荐好友入群\n"
             << "  5. 退出群\n  6. 踢出群成员\n  7. 审批入群申请\n  8. 设置/取消管理员\n"
             << "  9. 创建临时讨论组（子群）\n 10. 展示群特色功能\n 11. 动态切换群管理模式\n"
             << " 12. 转让群主\n 13. 讨论组管理（拉人 / 解散）\n"
             << " 14. 群聊天（群聊记录 / 发消息 / 实时群聊室）\n 15. 修改群名称、群公告\n 16. 禁言 / 解除禁言\n"
             << " 17. 新建群\n 18. 解散群\n  0. 返回\n";
        int choice = Util::readInt("请选择：", 0, 18);
        if (choice == 0) return;
        if (choice == 1) {
            listGroups();
            continue;
        }
        if (choice == 17) {
            createGroup();
            continue;
        }
        Group* group = askGroup();
        if (!group) continue;
        string msg;
        switch (choice) {
        case 2: showGroupDetail(*group); break;
        case 3:
            group->applyJoin(current_->id(), msg);
            cout << msg << "\n";
            break;
        case 4: inviteFriend(*group); break;
        case 5:
            if (Util::confirm("确认退出群 " + group->name() + "？(y/n)：")) {
                group->quit(current_->id(), msg);
                cout << msg << "\n";
            }
            break;
        case 6: kickMember(*group); break;
        case 7: approveApplications(*group); break;
        case 8: setAdmin(*group); break;
        case 9: createSubGroup(*group); break;
        case 10: showFeatures(*group); break;
        case 11: switchMode(*group); break;
        case 12: transferOwner(*group); break;
        case 13: manageSubGroup(*group); break;
        case 14: groupChat(*group); break;
        case 15: editGroupInfo(*group); break;
        case 16: muteMember(*group); break;
        case 18: dissolveGroup(*group); break;   // 解散后 group 已失效，紧接着回到菜单开头
        }
    }
}

void Menu::listGroups() const {
    std::vector<int> mine;
    for (Group* g : platform_.groups().groupsOf(current_->type(), current_->id())) mine.push_back(g->groupNo());
    std::map<int, int> unread = platform_.messages().unreadGroups(current_->type(), current_->id(), mine);
    for (Group* g : platform_.groups().groupsOfService(current_->type())) {
        cout << "  " << g->groupNo() << "  " << g->name() << "  [" << g->policy().modeName() << "]  群主："
             << (g->hasOwner() ? platform_.displayName(g->service(), g->ownerId()) : "无（可申请认领）")
             << "  成员 " << g->members().size() << " 人";
        if (g->isMember(current_->id())) {
            cout << "  （已加入";
            if (unread.count(g->groupNo())) cout << "，" << unread[g->groupNo()] << " 条未读";
            cout << "）";
        }
        cout << "\n";
    }
}

Group* Menu::askGroup() {
    int groupNo = Util::readInt("请输入群号：", 1, 99999999);
    Group* group = platform_.groups().find(current_->type(), groupNo);
    if (!group) cout << current_->serviceName() << "中不存在群 " << groupNo << "。\n";
    return group;
}

void Menu::showGroupDetail(const Group& group) const {
    const GroupPolicy& policy = group.policy();
    cout << "\n群号：" << group.groupNo() << "  群名：" << group.name() << "  管理模式：" << policy.modeName() << "\n";
    if (!group.hasOwner()) cout << "（该群原群主已注销且无人接任，第一个申请加入的人将成为新群主）\n";
    cout << "群公告：" << (group.notice().empty() ? "无" : group.notice()) << "\n";
    cout << "成员（" << group.members().size() << " 人）：\n";
    for (const string& id : group.members()) {
        cout << "  " << platform_.displayName(group.service(), id);
        if (group.isOwner(id)) cout << "  [群主]";
        else if (policy.allowAdmin() && group.isAdmin(id)) cout << "  [管理员]";
        if (policy.allowMute() && group.isMuted(id)) cout << "  [禁言]";
        cout << "\n";
    }
    if (!policy.allowAdmin() && !group.admins().empty())
        cout << "（原有 " << group.admins().size() << " 名管理员的数据已保留，当前模式下仅群主拥有特权）\n";
    if (!policy.allowMute() && !group.muted().empty())
        cout << "（原有 " << group.muted().size() << " 条禁言记录已保留，当前模式下不生效）\n";
    if (policy.allowApplyJoin()) cout << "待审批入群申请：" << group.pending().size() << " 条\n";
    if (policy.allowSubGroup()) {
        cout << "临时讨论组：" << (group.subGroups().empty() ? "无" : "") << "\n";
        for (const SubGroup& s : group.subGroups()) {
            cout << "  " << s.name() << "：";
            for (const string& id : s.members()) cout << platform_.displayName(group.service(), id) << " ";
            cout << "\n";
        }
    } else if (!group.subGroups().empty()) {
        cout << "（原有 " << group.subGroups().size() << " 个临时讨论组的数据已保留，切换回QQ群模式后可用）\n";
    }
}

void Menu::inviteFriend(Group& group) {
    std::vector<string> candidates;
    for (const Friend& f : current_->friends().all()) {
        if (!group.isMember(f.id())) candidates.push_back(f.id());
    }
    if (candidates.empty()) {
        cout << "没有可推荐的好友（好友都已在群中，或你还没有好友）。\n";
        return;
    }
    for (size_t i = 0; i < candidates.size(); ++i)
        cout << "  " << i + 1 << ". " << platform_.displayName(current_->type(), candidates[i]) << "\n";
    cout << "  0. 返回\n";
    int choice = Util::readInt("选择要推荐入群的好友：", 0, static_cast<int>(candidates.size()));
    if (choice == 0) return;
    string msg;
    group.invite(current_->id(), candidates[choice - 1], msg);
    cout << msg << "\n";
}

void Menu::kickMember(Group& group) {
    showGroupDetail(group);
    string target = Util::readWord("要踢出的成员账号：");
    string msg;
    group.kick(current_->id(), target, msg);
    cout << msg << "\n";
}

void Menu::approveApplications(Group& group) {
    if (!group.policy().allowApplyJoin()) {
        cout << "当前为" << group.policy().modeName() << "，没有入群申请制度。\n";
        return;
    }
    if (!group.policy().canApprove(group, current_->id())) {
        cout << "只有群主或管理员可以审批入群申请。\n";
        return;
    }
    while (!group.pending().empty()) {
        const std::vector<string>& pending = group.pending();
        for (size_t i = 0; i < pending.size(); ++i)
            cout << "  " << i + 1 << ". " << platform_.displayName(group.service(), pending[i]) << "\n";
        cout << "  0. 返回\n";
        int choice = Util::readInt("选择要处理的申请：", 0, static_cast<int>(pending.size()));
        if (choice == 0) return;
        string applicant = pending[choice - 1];
        bool accept = Util::confirm("是否同意入群？(y/n)：");
        string msg;
        group.approve(current_->id(), applicant, accept, msg);
        cout << msg << "\n";
    }
    cout << "暂无待审批的入群申请。\n";
}

void Menu::setAdmin(Group& group) {
    string target = Util::readWord("成员账号：");
    int choice = Util::readInt("1. 设为管理员  2. 取消管理员：", 1, 2);
    string msg;
    group.setAdmin(current_->id(), target, choice == 1, msg);
    cout << msg << "\n";
}

void Menu::createSubGroup(Group& group) {
    string name = Util::readWord("讨论组名称：");
    string line = Util::readLine("拉入的群成员账号（多个用英文逗号分隔）：");
    string msg;
    group.createSubGroup(current_->id(), name, Util::split(line, ','), msg);
    cout << msg << "\n";
}

void Menu::showFeatures(const Group& group) const {
    const GroupPolicy& policy = group.policy();
    cout << "\n群 " << group.groupNo() << "（" << group.name() << "）当前为【" << policy.modeName() << "】\n";
    policy.showFeatures();   // 多态：由当前策略对象展示各自的特色
    const string& me = current_->id();
    string role = group.isOwner(me) ? "群主"
                : (policy.allowAdmin() && group.isAdmin(me)) ? "管理员"
                : group.isMember(me) ? "普通成员" : "非成员";
    cout << "  你在本群的身份：" << role << "\n";
}

void Menu::switchMode(Group& group) {
    cout << "当前管理模式：" << group.policy().modeName() << "\n  1. QQ群模式\n  2. 微信群模式\n  0. 返回\n";
    int choice = Util::readInt("切换为：", 0, 2);
    if (choice == 0) return;
    // 切换前后各取一次“群主、成员、管理员、入群申请、讨论组”的完整快照，逐项比对而不只是比人数
    const string before = group.dataSnapshot();
    string msg;
    if (!group.switchMode(current_->id(), choice == 1 ? GroupMode::QQ : GroupMode::WECHAT, msg)) {
        cout << msg << "\n";
        return;
    }
    cout << msg << "\n切换前后逐项比对：群主、" << group.members().size() << " 名成员、" << group.admins().size()
         << " 条管理员记录、" << group.pending().size() << " 条入群申请、" << group.muted().size() << " 条禁言记录、群公告、"
         << group.subGroups().size() << " 个讨论组——"
         << (group.dataSnapshot() == before ? "完全一致，群成员数据未受影响。" : "出现差异，请检查！") << "\n";
    showFeatures(group);
}

void Menu::transferOwner(Group& group) {
    if (!group.isOwner(current_->id())) {
        cout << "只有群主可以转让群。\n";
        return;
    }
    showGroupDetail(group);
    string target = Util::readWord("新群主的账号：");
    if (!Util::confirm("确认把群主转让给 " + platform_.displayName(group.service(), target) + "？(y/n)：")) return;
    string msg;
    group.transferOwner(current_->id(), target, msg);
    cout << msg << "\n";
}

void Menu::manageSubGroup(Group& group) {
    if (group.subGroups().empty()) {
        cout << "本群还没有讨论组。\n";
        return;
    }
    const std::vector<SubGroup>& subs = group.subGroups();
    for (size_t i = 0; i < subs.size(); ++i) {
        cout << "  " << i + 1 << ". " << subs[i].name() << "（" << subs[i].members().size() << " 人，创建者 "
             << platform_.displayName(group.service(), subs[i].creator()) << "）\n";
    }
    cout << "  0. 返回\n";
    int choice = Util::readInt("选择讨论组：", 0, static_cast<int>(subs.size()));
    if (choice == 0) return;
    string subName = subs[choice - 1].name();   // 先复制名字：解散后 subs 中的元素会失效
    int action = Util::readInt("1. 拉群成员进入讨论组  2. 解散讨论组：", 1, 2);
    string msg;
    if (action == 1) {
        string line = Util::readLine("要拉入的群成员账号（多个用英文逗号分隔）：");
        for (const string& id : Util::split(line, ',')) {
            group.addToSubGroup(current_->id(), subName, id, msg);
            cout << msg << "\n";
        }
    } else if (Util::confirm("确认解散讨论组 " + subName + "？(y/n)：")) {
        group.dissolveSubGroup(current_->id(), subName, msg);
        cout << msg << "\n";
    }
}

void Menu::editGroupInfo(Group& group) {
    cout << "群名称：" << group.name() << "\n群公告：" << (group.notice().empty() ? "无" : group.notice()) << "\n"
         << "（" << group.policy().modeName() << "：" << (group.policy().allowAdmin() ? "群主和管理员" : "只有群主")
         << "可以修改）\n";
    int choice = Util::readInt("1. 修改群名称  2. 发布/修改群公告  3. 清除群公告  0. 返回：", 0, 3);
    string msg;
    if (choice == 1) {
        group.rename(current_->id(), Util::readWord("新群名称："), msg);
    } else if (choice == 2) {
        string notice = Util::readLine("公告内容：");
        if (notice.empty()) {
            cout << "公告内容不能为空（清除公告请选 3）。\n";
            return;
        }
        // 发布成功后同时作为一条群消息发出，群成员下次登录时会收到未读提醒
        if (group.setNotice(current_->id(), notice, msg))
            platform_.messages().postGroup(group.service(), group.groupNo(), current_->id(), "【群公告】" + group.notice());
    } else if (choice == 3) {
        group.setNotice(current_->id(), "", msg);
    } else {
        return;
    }
    cout << msg << "\n";
}

void Menu::muteMember(Group& group) {
    const GroupPolicy& policy = group.policy();
    if (!policy.allowMute()) {
        cout << "当前为" << policy.modeName() << "，没有禁言制度。";
        if (!group.muted().empty()) cout << "原有 " << group.muted().size() << " 条禁言记录已冻结保留，切换回QQ群模式后重新生效。";
        cout << "\n";
        return;
    }
    cout << "当前被禁言的成员：";
    if (group.muted().empty()) cout << "无";
    for (const string& id : group.muted()) cout << platform_.displayName(group.service(), id) << "  ";
    cout << "\n";
    string target = Util::readWord("成员账号：");
    int choice = Util::readInt("1. 禁言  2. 解除禁言：", 1, 2);
    string msg;
    group.setMuted(current_->id(), target, choice == 1, msg);
    cout << msg << "\n";
}

void Menu::createGroup() {
    string name = Util::readWord("新群名称：");
    string msg;
    platform_.createGroup(*current_, name, msg);
    cout << msg << "\n";
}

void Menu::dissolveGroup(const Group& group) {
    if (!group.isOwner(current_->id())) {
        cout << "只有群主可以解散群。\n";
        return;
    }
    const int groupNo = group.groupNo();   // 解散后 group 引用失效，先把需要的信息复制出来
    if (!Util::confirm("解散后成员、讨论组和群聊记录都将删除，无法恢复。确认解散群 " + std::to_string(groupNo) + "（" +
                         group.name() + "）？(y/n)："))
        return;
    string msg;
    platform_.dissolveGroup(*current_, groupNo, msg);
    cout << msg << "\n";
}

// ======================== 辅助函数 ========================

ServiceType Menu::askServiceType(const string& prompt) const {
    std::vector<ServiceType> all = ServiceTypeHelper::all();
    for (size_t i = 0; i < all.size(); ++i) cout << "  " << i + 1 << ". " << ServiceTypeHelper::name(all[i]) << "\n";
    return all[Util::readInt(prompt, 1, static_cast<int>(all.size())) - 1];
}

bool Menu::chooseOtherService(ServiceType& out) const {
    std::vector<ServiceBase*> others;
    for (ServiceBase* s : user_->services()) {
        if (s != current_) others.push_back(s);
    }
    if (others.empty()) {
        cout << "你只开通了一个服务，请先开通其他服务。\n";
        return false;
    }
    for (size_t i = 0; i < others.size(); ++i) cout << "  " << i + 1 << ". " << others[i]->serviceName() << "\n";
    cout << "  0. 返回\n";
    int choice = Util::readInt("选择另一个服务：", 0, static_cast<int>(others.size()));
    if (choice == 0) return false;
    out = others[choice - 1]->type();
    return true;
}

void Menu::askAccountInfo(string& nickname, Date& birthday, string& location, string& password) const {
    nickname = Util::readWord("昵称：");
    birthday = askBirthday();
    location = Util::readWord("所在地：");
    password = askNewPassword();
}

Date Menu::askBirthday() const {
    Date birthday;
    // yyyy-mm-dd 格式的字符串可以直接按字典序比较先后
    while (!Date::parse(Util::readWord("出生日期（如 2004-05-20）："), birthday) ||
           birthday.toString() > Date::today().toString())
        cout << "日期不正确（格式为 yyyy-mm-dd，且不能晚于今天），请重新输入。\n";
    return birthday;
}

string Menu::askNewPassword() const {
    while (true) {
        string first = Util::readWord("设置密码（至少 6 位）：");
        if (first.size() < 6) {
            cout << "密码太短，请至少输入 6 位。\n";
            continue;
        }
        if (Util::readWord("再次输入密码：") != first) {
            cout << "两次输入的密码不一致，请重新设置。\n";
            continue;
        }
        return first;
    }
}
