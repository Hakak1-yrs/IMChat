#include "FriendYD.h"

FriendYD::FriendYD(const std::string& id, const std::string& remark) : id_(id), remark_(remark) {}

const std::string& FriendYD::id() const { return id_; }

const std::string& FriendYD::remark() const { return remark_; }

void FriendYD::setRemark(const std::string& remark) { remark_ = remark; }
