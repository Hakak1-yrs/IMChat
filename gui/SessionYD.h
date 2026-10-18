#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "ConversationYD.h"
#include "PlatformYD.h"

// 图形界面的会话状态：当前登录的用户、正在使用的服务，以及界面需要的各种数据查询。
// 所有业务规则仍由 PlatformYD、GroupYD、MessageCenterYD 负责，这里只做“为界面整理数据”，
// 因此界面类只关心怎么画，不直接操作业务数据（与控制台版的 MenuYD 处于同一层次）
class SessionYD {
public:
    explicit SessionYD(PlatformYD& platform);

    // 登录：密码正确后，本人已开通的其他服务一并进入登录状态（题目：一处登录，其他服务视为自动登录）
    bool login(ServiceTypeYD type, const std::string& id, const std::string& password, std::string& msg);
    void logout();
    bool switchService(ServiceTypeYD type);
    // 关闭当前服务（删除账号及其好友、群数据），之后自动切换到本人仍在使用的其他服务
    bool closeCurrentService(std::string& msg);
    // 注销整个用户：删除全部服务账号后回到未登录状态
    void deleteCurrentUser(std::string& msg);
    bool loggedIn() const;
    UserYD* user() const;
    ServiceBaseYD* current() const;
    PlatformYD& platform();

    // ---- 列表 ----
    std::vector<ConversationYD> recentChats() const;   // 消息页：有过私信的好友 + 我加入的群，最近的在前
    std::vector<ConversationYD> contacts() const;      // 联系人页：全部好友
    std::vector<ConversationYD> myGroups() const;      // 群聊页：我加入的群
    int unreadOf(const ServiceBaseYD& account) const;  // 某个服务的未读总数（导航栏红点）

    // ---- 聊天 ----
    std::vector<MessageYD> messages(const ConversationYD& c) const;
    bool canSend(const ConversationYD& c, std::string& reason) const;   // 不能发言时给出原因（被禁言、已不是好友……）
    bool send(const ConversationYD& c, const std::string& text, std::string& msg);
    void markRead(const ConversationYD& c, int seq);
    std::string titleOf(const ConversationYD& c) const;
    GroupYD* groupOf(const ConversationYD& c) const;

    // 消息文件在上次检查之后是否有变化（本窗口、其他窗口或控制台程序发了新消息）
    bool messagesChanged();

private:
    void fill(ConversationYD& c) const;   // 填写名称、副标题

    PlatformYD& platform_;
    UserYD* user_ = nullptr;
    ServiceBaseYD* current_ = nullptr;
    std::filesystem::file_time_type stamp_{};
    std::uintmax_t size_ = 0;
};
