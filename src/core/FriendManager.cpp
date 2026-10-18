#include "FriendManager.h"
#include <algorithm>

bool FriendManager::add(const std::string& id, const std::string& remark) {
    if (contains(id)) return false;
    friends_.emplace_back(id, remark);
    return true;
}

bool FriendManager::remove(const std::string& id) {
    auto it = std::find_if(friends_.begin(), friends_.end(),
                           [&id](const Friend& f) { return f.id() == id; });
    if (it == friends_.end()) return false;
    friends_.erase(it);
    return true;
}

bool FriendManager::modifyRemark(const std::string& id, const std::string& remark) {
    for (Friend& f : friends_) {
        if (f.id() == id) {
            f.setRemark(remark);
            return true;
        }
    }
    return false;
}

const Friend* FriendManager::find(const std::string& id) const {
    for (const Friend& f : friends_) {
        if (f.id() == id) return &f;
    }
    return nullptr;
}

bool FriendManager::contains(const std::string& id) const { return find(id) != nullptr; }

const std::vector<Friend>& FriendManager::all() const { return friends_; }

size_t FriendManager::size() const { return friends_.size(); }
