#include "FriendManager.h"
#include <algorithm>

bool FriendManager::add(const std::string& id, const std::string& remark) {
    if (contains(id)) return false;
    index_[id] = friends_.size();
    friends_.emplace_back(id, remark);
    return true;
}

bool FriendManager::remove(const std::string& id) {
    auto it = index_.find(id);
    if (it == index_.end()) return false;
    const std::size_t position = it->second;
    index_.erase(it);
    friends_.erase(friends_.begin() + static_cast<std::ptrdiff_t>(position));   // 保持其余好友的先后顺序
    for (std::size_t i = position; i < friends_.size(); ++i) index_[friends_[i].id()] = i;   // 后面的都前移了一位
    return true;
}

bool FriendManager::modifyRemark(const std::string& id, const std::string& remark) {
    Friend* f = findMutable(id);
    if (!f) return false;
    f->setRemark(remark);
    return true;
}

const Friend* FriendManager::find(const std::string& id) const {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : &friends_[it->second];
}

Friend* FriendManager::findMutable(const std::string& id) {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : &friends_[it->second];
}

bool FriendManager::contains(const std::string& id) const { return find(id) != nullptr; }

const std::vector<Friend>& FriendManager::all() const { return friends_; }

size_t FriendManager::size() const { return friends_.size(); }
