#include "WeiShangService.h"
#include <iostream>

ServiceType WeiShangService::type() const { return ServiceType::WEISHANG; }

GroupMode WeiShangService::defaultGroupMode() const { return GroupMode::WECHAT; }

std::string WeiShangService::idLabel() const { return "微商号"; }

void WeiShangService::showInfo() const {
    IndependentIdService::showInfo();
    std::cout << "  说明：微商使用独立账号，群默认为微信群管理模式（只能推荐加入）\n";
}

std::string WeiShangService::momentsName() const { return "商品动态"; }
