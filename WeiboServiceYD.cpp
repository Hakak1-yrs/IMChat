#include "WeiboServiceYD.h"
#include <iostream>

ServiceTypeYD WeiboServiceYD::type() const { return ServiceTypeYD::WEIBO; }

bool WeiboServiceYD::usesQQNumber() const { return true; }

GroupModeYD WeiboServiceYD::defaultGroupMode() const { return GroupModeYD::QQ; }

std::string WeiboServiceYD::idLabel() const { return "微博号"; }

void WeiboServiceYD::showInfo() const {
    ServiceBaseYD::showInfo();
    std::cout << "  说明：微博号与 QQ 号共享\n";
}

std::string WeiboServiceYD::momentsName() const { return "微博动态"; }
