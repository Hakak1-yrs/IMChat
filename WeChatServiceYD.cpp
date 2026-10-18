#include "WeChatServiceYD.h"
#include <iostream>

ServiceTypeYD WeChatServiceYD::type() const { return ServiceTypeYD::WECHAT; }

GroupModeYD WeChatServiceYD::defaultGroupMode() const { return GroupModeYD::WECHAT; }

std::string WeChatServiceYD::idLabel() const { return "微信号"; }

void WeChatServiceYD::showInfo() const {
    IndependentIdServiceYD::showInfo();
    std::cout << "  说明：微信号独立于 QQ 号，群默认为微信群管理模式（只能推荐加入）\n";
}

std::string WeChatServiceYD::momentsName() const { return "朋友圈"; }
