#include "GroupPolicyYD.h"
#include "GroupYD.h"
#include "QQGroupPolicyYD.h"
#include "WeChatGroupPolicyYD.h"

bool GroupPolicyYD::canInvite(const GroupYD& group, const std::string& op) const {
    return group.isMember(op);
}

bool GroupPolicyYD::canSpeak(const GroupYD& group, const std::string& id) const {
    return group.isMember(id) && !(allowMute() && group.isMuted(id));
}

bool GroupPolicyYD::canRevoke(const GroupYD& group, const std::string& op, const std::string& sender) const {
    return canKick(group, op, sender);
}

std::unique_ptr<GroupPolicyYD> GroupPolicyYD::create(GroupModeYD mode) {
    if (mode == GroupModeYD::WECHAT) return std::make_unique<WeChatGroupPolicyYD>();
    return std::make_unique<QQGroupPolicyYD>();
}

std::string GroupPolicyYD::modeCode(GroupModeYD mode) {
    return mode == GroupModeYD::WECHAT ? "WECHAT" : "QQ";
}

bool GroupPolicyYD::parseMode(const std::string& code, GroupModeYD& mode) {
    if (code == "QQ") {
        mode = GroupModeYD::QQ;
        return true;
    }
    if (code == "WECHAT") {
        mode = GroupModeYD::WECHAT;
        return true;
    }
    return false;
}
