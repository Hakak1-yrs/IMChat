#include "QQService.h"
#include <iostream>

ServiceType QQService::type() const { return ServiceType::QQ; }

bool QQService::usesQQNumber() const { return true; }

GroupMode QQService::defaultGroupMode() const { return GroupMode::QQ; }

std::string QQService::idLabel() const { return "QQ号"; }

void QQService::showInfo() const {
    ServiceBase::showInfo();
    std::cout << "  说明：微博等服务可与此 QQ 号共享账号；支持点对点 TCP 聊天\n";
}

std::string QQService::momentsName() const { return "QQ空间"; }
