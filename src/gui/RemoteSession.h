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

class RemoteAccounts;

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

    // family：本人全部服务账号的集合（一处登录、多个服务同时在线），单独使用时为空
    RemoteSession(std::string host, std::uint16_t port, RemoteAccounts* family = nullptr);
    ~RemoteSession() override;

    void start(ServiceType type, const std::string& account, const std::string& password);
    void startWithToken(ServiceType type, const std::string& account, const std::string& token);
    bool pollChanges();               // 只处理自己的收件箱（changed() 还会顺带处理本人其他服务的）
    LoginState loginState();          // 登录页定时查看；同时处理收件箱
    std::string loginError() const;
    std::string server() const;       // "host:port"
    const std::string& token() const; // 登录令牌：开通新服务后凭它登录

    // 通用请求（个人中心、跨服务好友、群聊室、点对点信令……），done 在界面线程里调用
    using Result = std::function<void(bool ok, const nlohmann::json& data, const std::string& error)>;
    void call(const std::string& op, nlohmann::json args, Result done);
    // 推送来的点对点聊天信令（P2P_SIGNAL）、群聊室事件（ROOM_EVENT），取走后清空
    std::vector<nlohmann::json> takeSignals();
    std::vector<nlohmann::json> takeRoomEvents();
    void setProfile(const std::string& nickname, const std::string& location);   // 个人中心改了昵称、所在地

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
    std::vector<FriendRequestView> friendRequests() const override;
    bool requiresVerification() const override;
    std::vector<std::string> blockedList() const override;
    void loadProfile(const std::string& id) override;
    void requestFriend(const std::string& target, const std::string& greeting, const std::string& remark, Done done) override;
    void respondRequest(int seq, bool accept, const std::string& remark, Done done) override;
    void removeFriend(const std::string& id, Done done) override;
    void setRemark(const std::string& id, const std::string& remark, Done done) override;
    void setBlocked(const std::string& id, bool on, Done done) override;
    void setVerification(bool on, Done done) override;
    std::vector<GroupBrief> allGroups() const override;
    std::optional<GroupInfo> groupInfo(int groupNo) const override;
    void loadAllGroups() override;
    void loadGroup(int groupNo) override;
    void createGroup(const std::string& name, Created done) override;
    void groupCommand(const GroupCommand& command, Done done) override;
    std::optional<std::vector<Moment>> moments(const std::string& author) const override;
    void loadMoments(const std::string& author) override;
    std::vector<std::string> momentNoticeList(bool unreadOnly) const override;
    int momentNotices() const override;
    void markMomentsRead() override;
    void postMoment(const std::string& text, const std::string& scope, Done done) override;
    void toggleLike(int seq, Done done) override;
    void commentMoment(int seq, const std::string& replyTo, const std::string& text, Done done) override;
    void deleteMoment(int seq, Done done) override;
    void deleteComment(int commentSeq, Done done) override;
    int pendingFriendRequests() const override;
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
    // 发一个通用请求，成功后在界面线程里调用 apply；initial 为 true 表示是登录时的首次加载（计入加载进度）
    void fetch(const char* op, nlohmann::json args, std::function<void(const nlohmann::json&)> apply, bool initial);
    void reload(const std::string& topic);   // 重新取一类数据："contacts" / "social" / "groups" / "moments"
    void loadMomentNotices();
    // 修改类请求：成功后重新取 topics 对应的数据（以及 profileId 的资料），再把结果交给 done
    void mutate(const char* op, nlohmann::json args, std::vector<std::string> topics, const std::string& profileId, Done done);
    void applySocial(const nlohmann::json& data);
    void applyProfile(const nlohmann::json& data);
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
    RemoteAccounts* family_;
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
    std::vector<FriendRequestView> requests_;
    bool verify_ = true;
    std::vector<std::string> blocked_;
    std::map<std::string, ProfileView> profiles_;   // 打开过的资料页从服务端取回的完整资料
    std::map<int, GroupInfo> groupInfos_;           // 打开过的群设置页从服务端取回的群详情
    std::vector<GroupBrief> allGroups_;
    bool allGroupsWanted_ = false;                  // 打开过“全部群”
    std::map<std::string, std::vector<Moment>> moments_;   // 打开过的动态列表：作者（空表示全部）→ 动态
    std::vector<std::string> notices_;              // 朋友圈互动消息
    std::vector<std::string> unreadNotices_;
    bool dirty_ = false;
    std::string notice_;
    std::string status_;
    std::string token_;
    std::vector<nlohmann::json> signals_;
    std::vector<nlohmann::json> roomEvents_;
};
