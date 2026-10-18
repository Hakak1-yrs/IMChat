#pragma once
#include <optional>
#include <string>
#include <vector>
#include "Conversation.h"
#include "Message.h"
#include "ServiceType.h"

// ---- 界面使用的数据：只是为了显示而整理的简单结构，与数据来自本地还是服务端无关 ----

struct AccountView {   // 当前登录的服务账号
    ServiceType type = ServiceType::QQ;
    std::string id;
    std::string nickname;
    std::string location;
    std::string momentsName;   // QQ空间 / 朋友圈 / 微博动态 / 商品动态
};

struct MemberView {
    std::string id;
    std::string nickname;
    bool owner = false;
    bool admin = false;
    bool muted = false;
};

struct GroupView {   // 群资料（聊天面板的资料抽屉、@ 提醒）
    int groupNo = 0;
    std::string name;
    std::string modeName;
    std::string notice;
    std::vector<MemberView> members;
    bool canMentionAll = false;   // 我能不能 @所有人
};

struct ProfileView {   // 私信对方的资料（聊天面板的资料抽屉）
    bool known = false;                      // 账号存在
    std::string nickname;
    std::string location;
    std::string birthday;
    int tAge = 0;
    bool showOnline = false;                 // 联网模式才知道对方是否在线
    bool online = false;
    std::vector<std::string> mutualFriends;  // 共同好友的账号
};

struct PendingView {   // 联网模式：已经发出、还没收到服务端确认的消息（断线时会一直等到重连后补发）
    std::string text;
    bool failed = false;   // 服务端拒绝了（如对方已不是好友），error 是原因
    std::string error;
};

// 图形界面的会话：当前登录的用户和服务，以及界面需要的全部数据查询和操作。
// 界面只通过这个接口取数据、做操作，不接触业务对象，因此同一套界面有两种实现（桥接模式）：
// - LocalSession：直接读写本地数据文件（单机模式，功能最全）；
// - RemoteSession：通过网络连接 IMChatServer（联网模式，可以在不同电脑之间聊天）。
class Session {
public:
    virtual ~Session() = default;

    virtual bool isRemote() const = 0;
    virtual bool loggedIn() const = 0;
    virtual void logout() = 0;

    // ---- 当前账号 ----
    virtual AccountView me() const = 0;
    virtual std::vector<ServiceType> services() const = 0;   // 本人可以切换的服务（导航栏）
    virtual bool switchService(ServiceType type) = 0;
    virtual int unreadOf(ServiceType type) const = 0;        // 某个服务的未读总数（导航栏红点）

    // ---- 列表 ----
    virtual std::vector<Conversation> recentChats() const = 0;   // 消息页：有过消息的好友 + 我加入的群，最近的在前
    virtual std::vector<Conversation> contacts() const = 0;      // 联系人页：全部好友
    virtual std::vector<Conversation> myGroups() const = 0;      // 群聊页：我加入的群

    // ---- 聊天 ----
    virtual std::vector<Message> messages(const Conversation& c) const = 0;   // 按时间先后
    virtual std::vector<PendingView> pending(const Conversation& c) const;    // 还在等确认的消息（只有联网模式有）
    virtual int peerReadSeq(const Conversation& c) const = 0;                 // 私信的对方已读到哪一条（显示“已读”）
    virtual bool canSend(const Conversation& c, std::string& reason) const = 0;   // 不能发言时给出原因
    virtual bool send(const Conversation& c, const std::string& text, std::string& msg) = 0;
    virtual void markRead(const Conversation& c, int seq) = 0;
    virtual std::string titleOf(const Conversation& c) const = 0;
    virtual std::optional<GroupView> group(const Conversation& c) const = 0;
    virtual ProfileView profile(const std::string& id) const = 0;
    virtual std::string nicknameOf(const std::string& id) const = 0;

    // ---- 撤回与 @ ----
    virtual bool mentionsMe(const Message& m) const = 0;
    virtual bool canRevoke(const Message& m) const = 0;
    virtual void revoke(const Message& m) = 0;   // 失败的原因通过 takeNotice 告诉界面
    virtual std::string revokeText(const Message& m) const = 0;   // “小张”撤回了一条消息

    // ---- 刷新与提示 ----
    virtual bool changed() = 0;            // 自上次调用以来是否有新数据（新消息、其他窗口的修改……），有则界面刷新
    virtual std::string takeNotice();      // 需要提示用户的一句话，取走后清空
    virtual std::string status() const;    // 连接状态（联网模式断线时显示），正常时为空
    virtual int pendingFriendRequests() const;
    virtual int momentNotices() const;
};
