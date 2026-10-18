#pragma once
#include <string>

// 好友信息：好友在同一服务中的账号，以及我给他设置的备注
class FriendYD {
public:
    FriendYD(const std::string& id, const std::string& remark);

    const std::string& id() const;
    const std::string& remark() const;
    void setRemark(const std::string& remark);

private:
    std::string id_;
    std::string remark_;
};
