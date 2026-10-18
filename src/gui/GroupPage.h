#pragma once
#include "GroupService.h"
#include "Page.h"

// 群设置（对应控制台“群管理”中针对某个群的全部操作）：
// 2 群详情与成员  3 申请加入  4 推荐好友入群  5 退出群  6 踢出成员  7 审批入群申请  8 设置/取消管理员
// 9 创建讨论组  10 展示群特色  11 切换管理模式  12 转让群主  13 讨论组拉人/解散  15 群名称与群公告  16 禁言  18 解散群。
// 页面只通过 Session 取群详情（GroupInfo）、发出群命令（GroupCommand），单机和联网两种模式共用。
// 按钮是否出现由 GroupInfo 中的权限决定，这些权限由群当前的管理策略（GroupPolicy 的虚函数）算出，切换模式后页面随之变化
class GroupPage : public Page {
public:
    GroupPage(GuiWindow& window, int groupNo);
    void build(PageModel& model) override;
    void onAction(int action, const std::string& arg) override;
    std::string key() const override;

private:
    enum Action {
        ACT_RENAME = 1, ACT_NOTICE, ACT_CLEAR_NOTICE, ACT_SWITCH, ACT_JOIN, ACT_CHAT, ACT_ROOM, ACT_INVITE, ACT_QUIT,
        ACT_DISSOLVE, ACT_NEW_SUB, ACT_ADMIN_ON, ACT_ADMIN_OFF, ACT_MUTE_ON, ACT_MUTE_OFF, ACT_KICK, ACT_TRANSFER,
        ACT_APPROVE, ACT_REJECT, ACT_SUB_ADD, ACT_SUB_DISSOLVE, ACT_MEMBER
    };
    GroupCommand command(GroupCommand::Kind kind, const std::string& target = "", bool on = true) const;
    std::wstring name(const std::string& id);   // “昵称(账号)”
    // 先弹出确认框，确认后发出命令
    void confirm(const std::wstring& title, const std::wstring& text, const std::wstring& button, bool danger,
                 const GroupCommand& command);
    void inviteFriend();
    void switchMode(const GroupInfo& g);
    void editNotice(const GroupInfo& g);
    void createSubGroup(const GroupInfo& g);
    void addToSubGroup(const GroupInfo& g, const std::string& subName);

    int groupNo_;
};
