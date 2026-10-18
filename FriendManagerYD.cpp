#include "FriendManagerYD.h"
#include <algorithm>

bool FriendManagerYD::add(const std::string& id, const std::string& remark) {
    if (contains(id)) return false;
    friends_.emplace_back(id, remark);
    return true;
}

bool FriendManagerYD::remove(const std::string& id) {
    auto it = std::find_if(friends_.begin(), friends_.end(),
                           [&id](const FriendYD& f) { return f.id() == id; });
    if (it == friends_.end()) return false;
    friends_.erase(it);
    return true;
}

bool FriendManagerYD::modifyRemark(const std::string& id, const std::string& remark) {
    for (FriendYD& f : friends_) {
        if (f.id() == id) {
            f.setRemark(remark);
            return true;
        }
    }
    return false;
}

const FriendYD* FriendManagerYD::find(const std::string& id) const {
    for (const FriendYD& f : friends_) {
        if (f.id() == id) return &f;
    }
    return nullptr;
}

bool FriendManagerYD::contains(const std::string& id) const { return find(id) != nullptr; }

const std::vector<FriendYD>& FriendManagerYD::all() const { return friends_; }

size_t FriendManagerYD::size() const { return friends_.size(); }
