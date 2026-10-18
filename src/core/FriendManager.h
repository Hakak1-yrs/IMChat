#pragma once
#include <string>
#include <vector>
#include "Friend.h"

// 好友列表容器类：对一个服务账号的好友进行增、删、改、查
class FriendManager {
public:
    bool add(const std::string& id, const std::string& remark);
    bool remove(const std::string& id);
    bool modifyRemark(const std::string& id, const std::string& remark);
    const Friend* find(const std::string& id) const;
    bool contains(const std::string& id) const;

    const std::vector<Friend>& all() const;
    size_t size() const;

private:
    std::vector<Friend> friends_;
};
