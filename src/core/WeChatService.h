#pragma once
#include "IndependentIdService.h"

// 微信服务账号：使用独立的微信号，可以与本人的 QQ 号绑定（绑定功能继承自 IndependentIdService）；
// 群默认采用微信群管理模式
class WeChatService : public IndependentIdService {
public:
    using IndependentIdService::IndependentIdService;

    ServiceType type() const override;
    GroupMode defaultGroupMode() const override;
    std::string idLabel() const override;
    void showInfo() const override;
    std::string momentsName() const override;
};
