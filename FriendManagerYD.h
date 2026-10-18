#pragma once
#include <string>
#include <vector>
#include "FriendYD.h"

// 好友列表容器类：对一个服务账号的好友进行增、删、改、查
class FriendManagerYD {
public:
    bool add(const std::string& id, const std::string& remark);
    bool remove(const std::string& id);
    bool modifyRemark(const std::string& id, const std::string& remark);
    const FriendYD* find(const std::string& id) const;
    bool contains(const std::string& id) const;

    const std::vector<FriendYD>& all() const;
    size_t size() const;

private:
    std::vector<FriendYD> friends_;
};
