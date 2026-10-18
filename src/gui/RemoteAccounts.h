#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "RemoteSession.h"

// 联网模式下本人的全部服务账号（题目：一处登录，其他服务视为自动登录）。
// 用密码登录一个服务后，服务端在登录响应里给出本人开通的其他服务和一个登录令牌；
// 这里为每个服务各建一条连接（各一个 RemoteSession），凭令牌自动登录。
// 界面同一时间只显示其中一个（current），切换服务就是换一个 RemoteSession；
// 其他服务也一直在线、接收消息，导航栏上的未读红点因此是实时的
class RemoteAccounts {
public:
    RemoteAccounts(std::string host, std::uint16_t port);

    void start(ServiceType type, const std::string& account, const std::string& password);
    RemoteSession& current();
    RemoteSession* find(ServiceType type);
    std::vector<ServiceType> services() const;   // 已经登录好的服务（按 QQ、微信、微博、微商的顺序）
    bool switchTo(ServiceType type);

    // 以下由 RemoteSession 调用（都在界面线程里）
    void onLogin(const nlohmann::json& response);   // 密码登录成功：凭令牌登录其他服务
    bool pollOthers(const RemoteSession& except);   // 处理其他服务收到的数据，返回是否有变化

private:
    std::string host_;
    std::uint16_t port_;
    ServiceType current_ = ServiceType::QQ;
    std::map<ServiceType, std::unique_ptr<RemoteSession>> sessions_;
};
