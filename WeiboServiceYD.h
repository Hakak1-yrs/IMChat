#pragma once
#include "ServiceBaseYD.h"

// 微博服务账号：与 QQ 共享号码，群默认采用 QQ 群管理模式（可申请加入）
class WeiboServiceYD : public ServiceBaseYD {
public:
    using ServiceBaseYD::ServiceBaseYD;

    ServiceTypeYD type() const override;
    bool usesQQNumber() const override;
    GroupModeYD defaultGroupMode() const override;
    std::string idLabel() const override;
    void showInfo() const override;
    std::string momentsName() const override;
};
