#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "FriendRequest.h"
#include "MessageStore.h"

// 好友申请的内存索引：由消息存储里的申请（F）和处理结果（V）记录增量构建，按序号、按相关的人（申请人和对方）查找。
// 和朋友圈索引（MomentFeed）一样每次只应用新记录，账号注销（旧记录作废）后整体重建。
// 原来每次列出好友申请都要把全部申请读一遍，申请越多越慢
class FriendRequestBook {
public:
    explicit FriendRequestBook(const MessageStore& store);

    void refresh();
    const FriendRequest* find(int seq) const;
    std::vector<const FriendRequest*> of(ServiceType service, const std::string& id) const;   // 他发出的和收到的，按序号递增

private:
    static std::string key(ServiceType service, const std::string& id);

    const MessageStore& store_;
    int line_ = 0;
    int clears_ = 0;
    std::unordered_map<int, FriendRequest> requests_;                  // 申请序号 → 申请
    std::unordered_map<std::string, std::vector<int>> byAccount_;      // "服务 账号" → 和他有关的申请序号
};
