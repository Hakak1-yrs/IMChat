#pragma once
#include "ServiceBase.h"

// QQ 服务账号：使用 QQ 号码，群默认采用 QQ 群管理模式
class QQService : public ServiceBase {
public:
    using ServiceBase::ServiceBase;   // 继承基类构造函数

    ServiceType type() const override;
    bool usesQQNumber() const override;
    GroupMode defaultGroupMode() const override;
    std::string idLabel() const override;
    void showInfo() const override;
    std::string momentsName() const override;
};
