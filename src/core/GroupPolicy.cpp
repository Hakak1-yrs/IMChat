#include "GroupPolicy.h"
#include "Group.h"
#include "QQGroupPolicy.h"
#include "WeChatGroupPolicy.h"

bool GroupPolicy::canInvite(const Group& group, const std::string& op) const {
    return group.isMember(op);
}

bool GroupPolicy::canSpeak(const Group& group, const std::string& id) const {
    return group.isMember(id) && !(allowMute() && group.isMuted(id));
}

bool GroupPolicy::canRevoke(const Group& group, const std::string& op, const std::string& sender) const {
    return canKick(group, op, sender);
}

std::unique_ptr<GroupPolicy> GroupPolicy::create(GroupMode mode) {
    if (mode == GroupMode::WECHAT) return std::make_unique<WeChatGroupPolicy>();
    return std::make_unique<QQGroupPolicy>();
}

std::string GroupPolicy::modeCode(GroupMode mode) {
    return mode == GroupMode::WECHAT ? "WECHAT" : "QQ";
}

bool GroupPolicy::parseMode(const std::string& code, GroupMode& mode) {
    if (code == "QQ") {
        mode = GroupMode::QQ;
        return true;
    }
    if (code == "WECHAT") {
        mode = GroupMode::WECHAT;
        return true;
    }
    return false;
}
