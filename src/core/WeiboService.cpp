#include "WeiboService.h"
#include <iostream>

ServiceType WeiboService::type() const { return ServiceType::WEIBO; }

bool WeiboService::usesQQNumber() const { return true; }

GroupMode WeiboService::defaultGroupMode() const { return GroupMode::QQ; }

std::string WeiboService::idLabel() const { return "微博号"; }

void WeiboService::showInfo() const {
    ServiceBase::showInfo();
    std::cout << "  说明：微博号与 QQ 号共享\n";
}

std::string WeiboService::momentsName() const { return "微博动态"; }
