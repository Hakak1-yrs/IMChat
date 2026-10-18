#include "QQServiceYD.h"
#include <iostream>

ServiceTypeYD QQServiceYD::type() const { return ServiceTypeYD::QQ; }

bool QQServiceYD::usesQQNumber() const { return true; }

GroupModeYD QQServiceYD::defaultGroupMode() const { return GroupModeYD::QQ; }

std::string QQServiceYD::idLabel() const { return "QQ号"; }

void QQServiceYD::showInfo() const {
    ServiceBaseYD::showInfo();
    std::cout << "  说明：微博等服务可与此 QQ 号共享账号；支持点对点 TCP 聊天\n";
}

std::string QQServiceYD::momentsName() const { return "QQ空间"; }
