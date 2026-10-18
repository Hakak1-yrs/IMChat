#pragma once
#include <functional>
#include <string>
#include <vector>
#include "ChatLog.h"
#include "Platform.h"

class IndependentIdService;

// 菜单界面类：负责与用户交互，业务逻辑全部通过 Platform / Group 等类的接口完成
class Menu {
public:
    explicit Menu(Platform& platform);
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
    void bindQQ(IndependentIdService& account);
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
    void groupChat(Group& group);
    void searchGroupHistory(const Group& group) const;
    void groupChatRoom(Group& group, bool host);
    // 聊天窗口（私信和群聊天共用）：先显示最近的消息，之后每输入一行就发送一条，
    // 直接回车刷新（看到其他窗口刚发来的消息），输入 /q 返回；离开时把看过的消息记为已读
    void chatWindow(const std::string& title, bool group, const std::function<std::vector<Message>()>& load,
                    const std::function<bool(const std::string&, std::string&)>& send,
                    const std::function<void(int)>& markRead);
    void printMessage(const Message& m, size_t number) const;   // number 为 0 时不显示编号

    // ---- 朋友圈（QQ 空间 / 微博动态……）----
    void momentsMenu();
    void showMoments(const std::vector<Moment>& list) const;
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
    Group* askGroup();
    void showGroupDetail(const Group& group) const;
    void inviteFriend(Group& group);
    void kickMember(Group& group);
    void approveApplications(Group& group);
    void setAdmin(Group& group);
    void createSubGroup(Group& group);
    void showFeatures(const Group& group) const;
    void switchMode(Group& group);
    void transferOwner(Group& group);
    void manageSubGroup(Group& group);
    void editGroupInfo(Group& group);
    void muteMember(Group& group);
    void createGroup();
    void dissolveGroup(const Group& group);   // 解散成功后 group 引用失效，调用者不能再使用它

    // ---- 辅助 ----
    ServiceType askServiceType(const std::string& prompt) const;
    bool chooseOtherService(ServiceType& out) const;
    void askAccountInfo(std::string& nickname, Date& birthday, std::string& location, std::string& password) const;
    Date askBirthday() const;
    std::string askNewPassword() const;   // 输入两次、长度不少于 6 位

    Platform& platform_;
    User* user_;              // 当前登录的用户
    ServiceBase* current_;    // 当前正在操作的服务
    ChatLog chatLog_;         // 聊天记录（data\chat_log.txt）
};
