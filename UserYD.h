#pragma once
#include <map>
#include <string>
#include <vector>
#include "ServiceTypeYD.h"

class ServiceBaseYD;

// 平台用户（一个自然人）：可以开通 N 个微X服务，并统一管理这些服务的登录状态。
// 只保存指向服务账号的指针，账号对象由 PlatformYD 统一拥有和释放
class UserYD {
public:
    UserYD(int id = 0, const std::string& name = "");

    int id() const;
    const std::string& name() const;

    void attach(ServiceBaseYD* account);
    void detach(ServiceTypeYD type);                // 关闭某项服务时解除关联
    ServiceBaseYD* service(ServiceTypeYD type) const;
    bool hasService(ServiceTypeYD type) const;
    std::vector<ServiceBaseYD*> services() const;   // 已开通的全部服务

    bool isOnline() const;                          // 是否有任一服务在线
    void logoutAll();

private:
    int id_;
    std::string name_;
    std::map<ServiceTypeYD, ServiceBaseYD*> services_;
};
