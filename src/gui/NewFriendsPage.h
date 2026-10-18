#pragma once
#include "Page.h"

// 新的朋友（对应控制台“好友管理 → 9 新的朋友 / 10 黑名单 / 11 好友验证设置”）：
// 收到的好友申请可以同意（顺便设置备注）或拒绝，发出的申请显示对方是否已通过；
// 还有“加我为好友时需要验证”开关和黑名单
class NewFriendsPage : public Page {
public:
    explicit NewFriendsPage(GuiWindow& window);
    void build(PageModel& model) override;
    void onAction(int action, const std::string& arg) override;
    std::string key() const override;

private:
    enum Action { ACT_ADD = 1, ACT_ACCEPT, ACT_REJECT, ACT_VERIFY, ACT_UNBLOCK, ACT_BLOCK, ACT_PROFILE };
};
