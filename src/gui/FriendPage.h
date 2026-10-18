#pragma once
#include "Page.h"

// 好友资料（对应控制台“好友管理 → 3. 修改好友备注 / 4. 删除好友 / 8. 查看好友资料（含共同好友）”）：
// 显示对方资料（虚函数 showInfo()）、我给他的备注、我们在同一服务中的共同好友；可以发消息、修改备注、删除好友
class FriendPage : public Page {
public:
    FriendPage(GuiWindow& window, const std::string& peerId);
    void build(PageModel& model) override;
    void onAction(int action, const std::string& arg) override;
    std::string key() const override;
    static void addFriendDialog(GuiWindow& window);   // 添加好友（对应控制台“好友管理 → 2. 添加好友”）

private:
    enum Action { ACT_CHAT = 1, ACT_REMARK, ACT_DELETE, ACT_ADD, ACT_MOMENTS, ACT_BLOCK };
    std::string peerId_;
};
