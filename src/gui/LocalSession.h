#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "Platform.h"
#include "Session.h"

// 单机模式的会话：直接使用 Platform 读写本地数据文件。
// 所有业务规则仍由 Platform、Group、MessageCenter 负责，这里只做“为界面整理数据”。
// 除了 Session 接口，还提供业务对象本身（user / current / platform），供只在单机模式下开放的功能页使用
class LocalSession : public Session {
public:
    explicit LocalSession(Platform& platform);

    // 登录：密码正确后，本人已开通的其他服务一并进入登录状态（题目：一处登录，其他服务视为自动登录）
    bool login(ServiceType type, const std::string& id, const std::string& password, std::string& msg);
    // 关闭当前服务（删除账号及其好友、群数据），之后自动切换到本人仍在使用的其他服务
    bool closeCurrentService(std::string& msg);
    // 注销整个用户：删除全部服务账号后回到未登录状态
    void deleteCurrentUser(std::string& msg);
    User* user() const;
    ServiceBase* current() const;
    Platform& platform();
    Group* groupOf(const Conversation& c) const;

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
    void requestFriend(const std::string& target, const std::string& greeting, const std::string& remark, Done done) override;
    void respondRequest(int seq, bool accept, const std::string& remark, Done done) override;
    void removeFriend(const std::string& id, Done done) override;
    void setRemark(const std::string& id, const std::string& remark, Done done) override;
    void setBlocked(const std::string& id, bool on, Done done) override;
    void setVerification(bool on, Done done) override;
    std::vector<GroupBrief> allGroups() const override;
    std::optional<GroupInfo> groupInfo(int groupNo) const override;
    void createGroup(const std::string& name, Created done) override;
    void groupCommand(const GroupCommand& command, Done done) override;
    std::optional<std::vector<Moment>> moments(const std::string& author) const override;
    std::vector<std::string> momentNoticeList(bool unreadOnly) const override;
    void markMomentsRead() override;
    void postMoment(const std::string& text, const std::string& scope, Done done) override;
    void toggleLike(int seq, Done done) override;
    void commentMoment(int seq, const std::string& replyTo, const std::string& text, Done done) override;
    void deleteMoment(int seq, Done done) override;
    void deleteComment(int commentSeq, Done done) override;
    bool changed() override;
    std::string takeNotice() override;
    int pendingFriendRequests() const override;
    int momentNotices() const override;

private:
    void fill(Conversation& c) const;   // 填写名称、副标题

    Platform& platform_;
    User* user_ = nullptr;
    ServiceBase* current_ = nullptr;
    std::filesystem::file_time_type stamp_{};
    std::uintmax_t size_ = 0;
    std::string notice_;
};
