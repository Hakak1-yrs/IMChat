#pragma once
#include "PageYD.h"
#include "ServiceTypeYD.h"

// 跨服务好友（对应控制台“好友管理 → 6. 从其他服务的好友中添加（推荐好友）/ 7. 查询与其他服务的共同好友”）：
// 选择本人开通的另一个服务，依据那里的好友推荐添加，并列出两边都是好友的人；
// 无法确认是同一人（独立号码的服务未绑定 QQ）的，列出原因
class CrossFriendPageYD : public PageYD {
public:
    explicit CrossFriendPageYD(GuiWindowYD& window);
    void build(PageModelYD& model) override;
    void onAction(int action, const std::string& arg) override;

private:
    enum ActionYD { ACT_SOURCE = 1, ACT_ADD, ACT_ACCOUNT };
    bool hasSource_ = false;
    ServiceTypeYD source_ = ServiceTypeYD::QQ;
};
