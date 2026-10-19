#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "Friend.h"

// 好友列表容器类：对一个服务账号的好友进行增、删、改、查。
// 好友按添加的先后保存；另有一个哈希索引（账号 → 位置），查找是常数时间——
// 判断是否互为好友、算共同好友、朋友圈的可见性都要频繁查找，逐个比较时联系人列表的开销是好友数的三次方
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
    Friend* findMutable(const std::string& id);

    std::vector<Friend> friends_;
    std::unordered_map<std::string, std::size_t> index_;   // 账号 → 在 friends_ 中的位置
};
