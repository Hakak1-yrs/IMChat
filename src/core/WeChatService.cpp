#include "WeChatService.h"
#include <iostream>

ServiceType WeChatService::type() const { return ServiceType::WECHAT; }

GroupMode WeChatService::defaultGroupMode() const { return GroupMode::WECHAT; }

std::string WeChatService::idLabel() const { return "微信号"; }

void WeChatService::showInfo() const {
    IndependentIdService::showInfo();
    std::cout << "  说明：微信号独立于 QQ 号，群默认为微信群管理模式（只能推荐加入）\n";
}

std::string WeChatService::momentsName() const { return "朋友圈"; }
