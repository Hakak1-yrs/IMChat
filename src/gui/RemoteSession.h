#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "ChatClient.h"
#include "Session.h"

// 联网模式的会话：通过 ChatClient 连接 IMChatServer，界面需要的数据保存在本地缓存里。
//
// 线程：ChatClient 的回调在网络线程里执行，这里不直接改缓存，而是把“要做的修改”放进收件箱；
// 界面线程定时调用 changed()（或登录时调用 loginState()）时取出来依次执行。
// 这样缓存只在界面线程里读写，界面代码不需要考虑加锁。
//
// 数据来源：登录后服务端补发历史消息（同步），再取联系人、群资料、已读位置；
// 之后靠推送保持最新：新消息、已读回执、好友上下线、消息被撤回。断线重连后会重新取一遍
class RemoteSession : public Session {
public:
    enum class LoginState { CONNECTING, LOADING, READY, FAILED };

    RemoteSession(std::string host, std::uint16_t port);
    ~RemoteSession() override;

    void start(ServiceType type, const std::string& account, const std::string& password);
    LoginState loginState();          // 登录页定时查看；同时处理收件箱
    std::string loginError() const;
    std::string server() const;       // "host:port"

    bool isRemote() const override;
    bool loggedIn() const override;
    void logout() override;
    AccountView me() const override;
    std::vector<ServiceType> services() const override;
    bool switchService(ServiceType type) override;
    int unreadOf(ServiceType type) const override;
    std::vector<Conversation> recentChats() const override;
    std::vector<Conversation> contacts() const override;
    std::vector<Conversation> myGroups() const override;
    std::vector<Message> messages(const Conversation& c) const override;
    std::vector<PendingView> pending(const Conversation& c) const override;
    int peerReadSeq(const Conversation& c) const override;
    bool canSend(const Conversation& c, std::string& reason) const override;
    bool send(const Conversation& c, const std::string& text, std::string& msg) override;
    void markRead(const Conversation& c, int seq) override;
    std::string titleOf(const Conversation& c) const override;
    std::optional<GroupView> group(const Conversation& c) const override;
    ProfileView profile(const std::string& id) const override;
    std::string nicknameOf(const std::string& id) const override;
    bool mentionsMe(const Message& m) const override;
    bool canRevoke(const Message& m) const override;
    void revoke(const Message& m) override;
    std::string revokeText(const Message& m) const override;
    bool changed() override;
    std::string takeNotice() override;
    std::string status() const override;

private:
    struct FriendEntry {
        std::string nickname;
        std::string remark;
        std::string location;
        std::string birthday;
        int tAge = 0;
        bool online = false;
        std::vector<std::string> mutual;
    };
    struct GroupEntry {
        GroupView view;
        bool canSpeak = true;
        std::set<std::string> revocable;   // 我能撤回哪些成员的消息（服务端按群的管理策略算好）
    };
    struct PendingEntry {
        std::string clientMsgId;
        std::string key;   // 会话标识，如 "P:10002"
        PendingView view;
    };

    void post(std::function<void()> change);   // 网络线程调用：放进收件箱
    void drain();                              // 界面线程调用：执行收件箱里的修改
    ChatClient::Callbacks callbacks();
    void onLogin(bool ok, const nlohmann::json& response);
    void loadLists();
    void listLoaded();
    void applyMessage(const nlohmann::json& message);
    void applyAck(const nlohmann::json& ack);
    void applyContacts(const nlohmann::json& data);
    void applyGroups(const nlohmann::json& data);
    void applyReads(const nlohmann::json& data);
    std::string keyOf(const Message& m) const;
    void fill(Conversation& c) const;
    int unreadIn(const std::string& key, bool* mentioned) const;
    const GroupEntry* groupEntry(int groupNo) const;

    std::string host_;
    std::uint16_t port_;
    std::unique_ptr<ChatClient> client_;
    std::mutex mutex_;
    std::vector<std::function<void()>> inbox_;   // 受 mutex_ 保护

    // ---- 以下只在界面线程里访问 ----
    LoginState loginState_ = LoginState::CONNECTING;
    std::string loginError_;
    bool loggedIn_ = false;
    int listsLoading_ = 0;
    AccountView me_;
    std::vector<std::string> friendOrder_;
    std::map<std::string, FriendEntry> friends_;
    std::vector<int> groupOrder_;
    std::map<int, GroupEntry> groups_;
    std::map<int, Message> messages_;          // 序号 → 消息
    std::set<int> mentionsMe_;                 // @ 了我的消息
    std::map<std::string, int> readMarks_;     // 会话 → 我读到的序号
    std::map<std::string, int> peerRead_;      // 好友 → 对方读到我发的哪一条
    std::map<std::string, std::string> nicknames_;
    std::vector<PendingEntry> pending_;
    bool dirty_ = false;
    std::string notice_;
    std::string status_;
};
