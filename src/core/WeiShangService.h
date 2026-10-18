#pragma once
#include "IndependentIdService.h"

// 微商服务账号：题目中“其他微X产品”的代表。
// 与微信同属“独立 ID”一类：使用 ws 开头的独立账号，同样可以绑定本人的 QQ 号；群默认采用微信群管理模式。
// 新增该服务只增加了这一个派生类，并在类型枚举和工厂函数中各加一项，体现可扩展性。
class WeiShangService : public IndependentIdService {
public:
    using IndependentIdService::IndependentIdService;

    ServiceType type() const override;
    GroupMode defaultGroupMode() const override;
    std::string idLabel() const override;
    void showInfo() const override;
    std::string momentsName() const override;
};
