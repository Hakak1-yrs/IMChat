#pragma once
#include <string>
#include "Date.h"
#include "FriendManager.h"
#include "GroupPolicy.h"
#include "ServiceType.h"

// 微X服务账号抽象基类：保存各服务共有的用户基本信息与好友列表，
// 各服务的差异通过虚函数由派生类实现
class ServiceBase {
public:
    ServiceBase(const std::string& id, int ownerId, const std::string& nickname,
                  const Date& birthday, const Date& registerDate,
                  const std::string& location, const std::string& password);
    virtual ~ServiceBase() = default;

    // ---- 由派生类实现的多态接口 ----
    virtual ServiceType type() const = 0;
    virtual bool usesQQNumber() const = 0;            // 是否使用 QQ 号码作为账号（微博与 QQ 共享 ID）
    virtual GroupMode defaultGroupMode() const = 0; // 本服务的群默认采用哪种管理模式
    virtual std::string idLabel() const = 0;          // 账号的称呼，如“QQ号”“微信号”
    virtual void showInfo() const;                    // 显示个人信息，派生类可追加特有信息
    virtual std::string extraData() const;            // 派生类特有字段，存盘时使用
    virtual void loadExtraData(const std::string& data);
    // 本账号对应的 QQ 号：共享号码的服务（QQ、微博）就是自己的号码；
    // 独立号码的服务（微信、微商）是绑定的 QQ 号，未绑定时为空。
    // 两个账号对应同一个 QQ 号，平台才认定它们属于同一个人（跨服务共同好友、推荐好友都依赖它）
    virtual std::string linkedQQ() const;
    // 动态（朋友圈）功能在各服务中的叫法：QQ 空间、微信朋友圈、微博动态……
    virtual std::string momentsName() const = 0;

    std::string serviceName() const;
    std::string displayName() const;                  // 昵称(账号)

    const std::string& id() const;
    int ownerId() const;
    const std::string& nickname() const;
    void setNickname(const std::string& nickname);
    const Date& birthday() const;
    void setBirthday(const Date& birthday);
    const Date& registerDate() const;
    int tAge() const;                                 // T龄：号码申请至今的年数
    const std::string& location() const;
    void setLocation(const std::string& location);
    const std::string& password() const;              // 返回加密后的密码记录（盐$摘要），用于存盘
    bool checkPassword(const std::string& password) const;
    void setPassword(const std::string& plain);       // 修改密码：保存新的加密记录

    bool isLoggedIn() const;
    void login();
    void logout();

    FriendManager& friends();
    const FriendManager& friends() const;

protected:
    std::string id_;
    int ownerId_;                // 所属用户（自然人）编号
    std::string nickname_;
    Date birthday_;
    Date registerDate_;
    std::string location_;
    std::string password_;       // 加密记录，不是明文
    bool loggedIn_;
    FriendManager friends_;
};
