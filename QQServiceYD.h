#pragma once
#include "ServiceBaseYD.h"

// QQ 服务账号：使用 QQ 号码，群默认采用 QQ 群管理模式
class QQServiceYD : public ServiceBaseYD {
public:
    using ServiceBaseYD::ServiceBaseYD;   // 继承基类构造函数

    ServiceTypeYD type() const override;
    bool usesQQNumber() const override;
    GroupModeYD defaultGroupMode() const override;
    std::string idLabel() const override;
    void showInfo() const override;
    std::string momentsName() const override;
};
