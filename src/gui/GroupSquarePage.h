#pragma once
#include "Page.h"

// 全部群（对应控制台“群管理 → 1. 查看本服务所有群 / 3. 申请加入群 / 17. 新建群”）：
// 列出当前服务的所有群及其管理模式、群主、人数；未加入的群可以申请加入（无主群可以认领），也可以新建群
class GroupSquarePage : public Page {
public:
    explicit GroupSquarePage(GuiWindow& window);
    void build(PageModel& model) override;
    void onAction(int action, const std::string& arg) override;
    static void createGroupDialog(GuiWindow& window);

private:
    enum Action { ACT_CREATE = 1, ACT_DETAIL, ACT_CHAT, ACT_JOIN };
};
