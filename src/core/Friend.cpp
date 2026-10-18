#include "Friend.h"

Friend::Friend(const std::string& id, const std::string& remark) : id_(id), remark_(remark) {}

const std::string& Friend::id() const { return id_; }

const std::string& Friend::remark() const { return remark_; }

void Friend::setRemark(const std::string& remark) { remark_ = remark; }
