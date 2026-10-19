#include "FriendRequestBook.h"
#include <algorithm>
#include <cstdlib>

FriendRequestBook::FriendRequestBook(const MessageStore& store) : store_(store) {}

std::string FriendRequestBook::key(ServiceType service, const std::string& id) { return ServiceTypeHelper::code(service) + " " + id; }

void FriendRequestBook::refresh() {
    const int clears = store_.recordClears();
    if (clears != clears_) {   // 有账号注销、旧记录少了一部分：从头重建
        requests_.clear();
        byAccount_.clear();
        line_ = 0;
        clears_ = clears;
    }
    const int end = store_.lastRecordLine();
    if (end <= line_) return;
    int last = end;
    for (const Record& r : store_.recordsAfter("FV", line_)) {
        last = (std::max)(last, r.line);
        if (r.kind == 'F') {   // 对方 申请人 备注（没有时为 -），行尾是验证消息
            FriendRequest& q = requests_[r.seq];
            q.seq = r.seq;
            q.service = r.service;
            q.to = r.field(0);
            q.from = r.field(1);
            q.remark = r.field(2) == "-" ? "" : r.field(2);
            q.greeting = r.text;
            q.time = r.time;
            byAccount_[key(r.service, q.to)].push_back(r.seq);
            if (q.from != q.to) byAccount_[key(r.service, q.from)].push_back(r.seq);
        } else {               // 申请序号 A/X 处理人 备注：以最后一次处理为准
            auto it = requests_.find(std::atoi(r.field(0).c_str()));
            if (it != requests_.end()) it->second.status = r.field(1) == "A" ? FriendRequest::ACCEPTED : FriendRequest::REJECTED;
        }
    }
    line_ = last;
}

const FriendRequest* FriendRequestBook::find(int seq) const {
    auto it = requests_.find(seq);
    return it == requests_.end() ? nullptr : &it->second;
}

std::vector<const FriendRequest*> FriendRequestBook::of(ServiceType service, const std::string& id) const {
    std::vector<const FriendRequest*> result;
    auto it = byAccount_.find(key(service, id));
    if (it == byAccount_.end()) return result;
    for (int seq : it->second) {
        if (const FriendRequest* q = find(seq)) result.push_back(q);
    }
    return result;
}
