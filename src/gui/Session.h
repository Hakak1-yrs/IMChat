#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "Conversation.h"
#include "Platform.h"

// 图形界面的会话状态：当前登录的用户、正在使用的服务，以及界面需要的各种数据查询。
// 所有业务规则仍由 Platform、Group、MessageCenter 负责，这里只做“为界面整理数据”，
// 因此界面类只关心怎么画，不直接操作业务数据（与控制台版的 Menu 处于同一层次）
class Session {
public:
    explicit Session(Platform& platform);

    // 登录：密码正确后，本人已开通的其他服务一并进入登录状态（题目：一处登录，其他服务视为自动登录）
    bool login(ServiceType type, const std::string& id, const std::string& password, std::string& msg);
    void logout();
    bool switchService(ServiceType type);
    // 关闭当前服务（删除账号及其好友、群数据），之后自动切换到本人仍在使用的其他服务
    bool closeCurrentService(std::string& msg);
    // 注销整个用户：删除全部服务账号后回到未登录状态
    void deleteCurrentUser(std::string& msg);
    bool loggedIn() const;
    User* user() const;
    ServiceBase* current() const;
    Platform& platform();

    // ---- 列表 ----
    std::vector<Conversation> recentChats() const;   // 消息页：有过私信的好友 + 我加入的群，最近的在前
    std::vector<Conversation> contacts() const;      // 联系人页：全部好友
    std::vector<Conversation> myGroups() const;      // 群聊页：我加入的群
    int unreadOf(const ServiceBase& account) const;  // 某个服务的未读总数（导航栏红点）

    // ---- 聊天 ----
    std::vector<Message> messages(const Conversation& c) const;
    bool canSend(const Conversation& c, std::string& reason) const;   // 不能发言时给出原因（被禁言、已不是好友……）
    bool send(const Conversation& c, const std::string& text, std::string& msg);
    void markRead(const Conversation& c, int seq);
    std::string titleOf(const Conversation& c) const;
    Group* groupOf(const Conversation& c) const;

    // 消息文件在上次检查之后是否有变化（本窗口、其他窗口或控制台程序发了新消息）
    bool messagesChanged();

private:
    void fill(Conversation& c) const;   // 填写名称、副标题

    Platform& platform_;
    User* user_ = nullptr;
    ServiceBase* current_ = nullptr;
    std::filesystem::file_time_type stamp_{};
    std::uintmax_t size_ = 0;
};
