#include "FriendRequestYD.h"

std::string FriendRequestYD::statusText() const {
    switch (status) {
    case ACCEPTED: return "已添加";
    case REJECTED: return "已拒绝";
    default: return "等待验证";
    }
}
