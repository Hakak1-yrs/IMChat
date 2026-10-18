#pragma once
#include "IndependentIdServiceYD.h"

// 微信服务账号：使用独立的微信号，可以与本人的 QQ 号绑定（绑定功能继承自 IndependentIdServiceYD）；
// 群默认采用微信群管理模式
class WeChatServiceYD : public IndependentIdServiceYD {
public:
    using IndependentIdServiceYD::IndependentIdServiceYD;

    ServiceTypeYD type() const override;
    GroupModeYD defaultGroupMode() const override;
    std::string idLabel() const override;
    void showInfo() const override;
    std::string momentsName() const override;
};
