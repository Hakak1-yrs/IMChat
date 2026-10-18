#include "WeiShangServiceYD.h"
#include <iostream>

ServiceTypeYD WeiShangServiceYD::type() const { return ServiceTypeYD::WEISHANG; }

GroupModeYD WeiShangServiceYD::defaultGroupMode() const { return GroupModeYD::WECHAT; }

std::string WeiShangServiceYD::idLabel() const { return "微商号"; }

void WeiShangServiceYD::showInfo() const {
    IndependentIdServiceYD::showInfo();
    std::cout << "  说明：微商使用独立账号，群默认为微信群管理模式（只能推荐加入）\n";
}

std::string WeiShangServiceYD::momentsName() const { return "商品动态"; }
