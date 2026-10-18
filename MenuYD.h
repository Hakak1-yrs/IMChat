#pragma once
#include <functional>
#include <string>
#include <vector>
#include "ChatLogYD.h"
#include "PlatformYD.h"

class IndependentIdServiceYD;

// 菜单界面类：负责与用户交互，业务逻辑全部通过 PlatformYD / GroupYD 等类的接口完成
class MenuYD {
public:
    explicit MenuYD(PlatformYD& platform);
    void run();

private:
    // ---- 主菜单 ----
    void login();
    void registerUser();
    void showOverview() const;

    // ---- 登录后的用户菜单 ----
    void userMenu();
    void printStatus() const;
    void switchService();
    void openService();
    void showProfile();
    void bindQQ(IndependentIdServiceYD& account);
    void changePassword();
    bool closeCurrentService();     // 返回 true 表示当前服务已关闭，需要退出个人信息菜单
    bool deleteAccount();           // 返回 true 表示账号已注销
    void tcpChat();

    // ---- 消息中心：好友私信、群聊天 ----
    void showUnreadSummary() const;   // 登录后提示各个在线服务的未读消息
    std::string unreadLabel() const;  // 菜单上显示的“N 条未读”
    void messageCenter();
    void choosePrivateChat();
    void privateChat(const std::string& friendId);
    void chooseGroupChat();
    void groupChat(GroupYD& group);
    void searchGroupHistory(const GroupYD& group) const;
    void groupChatRoom(GroupYD& group, bool host);
    // 聊天窗口（私信和群聊天共用）：先显示最近的消息，之后每输入一行就发送一条，
    // 直接回车刷新（看到其他窗口刚发来的消息），输入 /q 返回；离开时把看过的消息记为已读
    void chatWindow(const std::string& title, bool group, const std::function<std::vector<MessageYD>()>& load,
                    const std::function<bool(const std::string&, std::string&)>& send,
                    const std::function<void(int)>& markRead);
    void printMessage(const MessageYD& m, size_t number) const;   // number 为 0 时不显示编号

    // ---- 朋友圈（QQ 空间 / 微博动态……）----
    void momentsMenu();
    void showMoments(const std::vector<MomentYD>& list) const;
    void postMoment();
    int askMoment(const std::string& prompt) const;

    // ---- 好友申请、黑名单 ----
    void newFriends();
    void blacklistMenu();

    // ---- 好友管理 ----
    void friendMenu();
    void listFriends() const;
    void addFriend();
    void modifyFriend();
    void deleteFriend();
    void searchFriend() const;
    void addFromOtherService();
    void showCommonFriends();
    void showFriendDetail();

    // ---- 群管理 ----
    void groupMenu();
    void listGroups() const;
    GroupYD* askGroup();
    void showGroupDetail(const GroupYD& group) const;
    void inviteFriend(GroupYD& group);
    void kickMember(GroupYD& group);
    void approveApplications(GroupYD& group);
    void setAdmin(GroupYD& group);
    void createSubGroup(GroupYD& group);
    void showFeatures(const GroupYD& group) const;
    void switchMode(GroupYD& group);
    void transferOwner(GroupYD& group);
    void manageSubGroup(GroupYD& group);
    void editGroupInfo(GroupYD& group);
    void muteMember(GroupYD& group);
    void createGroup();
    void dissolveGroup(const GroupYD& group);   // 解散成功后 group 引用失效，调用者不能再使用它

    // ---- 辅助 ----
    ServiceTypeYD askServiceType(const std::string& prompt) const;
    bool chooseOtherService(ServiceTypeYD& out) const;
    void askAccountInfo(std::string& nickname, DateYD& birthday, std::string& location, std::string& password) const;
    DateYD askBirthday() const;
    std::string askNewPassword() const;   // 输入两次、长度不少于 6 位

    PlatformYD& platform_;
    UserYD* user_;              // 当前登录的用户
    ServiceBaseYD* current_;    // 当前正在操作的服务
    ChatLogYD chatLog_;         // 聊天记录（data\chat_log.txt）
};
