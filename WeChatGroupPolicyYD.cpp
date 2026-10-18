#include "WeChatGroupPolicyYD.h"
#include <iostream>
#include "GroupYD.h"

GroupModeYD WeChatGroupPolicyYD::mode() const { return GroupModeYD::WECHAT; }

std::string WeChatGroupPolicyYD::modeName() const { return "微信群模式"; }

bool WeChatGroupPolicyYD::allowApplyJoin() const { return false; }

bool WeChatGroupPolicyYD::allowSubGroup() const { return false; }

bool WeChatGroupPolicyYD::allowAdmin() const { return false; }

bool WeChatGroupPolicyYD::allowMute() const { return false; }

bool WeChatGroupPolicyYD::canApprove(const GroupYD&, const std::string&) const { return false; }

bool WeChatGroupPolicyYD::canKick(const GroupYD& group, const std::string& op, const std::string& target) const {
    return group.isOwner(op) && op != target && group.isMember(target);
}

bool WeChatGroupPolicyYD::canMute(const GroupYD&, const std::string&, const std::string&) const { return false; }

bool WeChatGroupPolicyYD::canEditInfo(const GroupYD& group, const std::string& op) const { return group.isOwner(op); }

void WeChatGroupPolicyYD::showFeatures() const {
    std::cout << "  【微信群特色】\n"
              << "  1. 入群方式：不能主动申请，只能由群成员推荐（邀请）好友加入\n"
              << "  2. 管理制度：没有管理员，仅群主为特权账号\n"
              << "  3. 权限：只有群主可以踢出成员\n"
              << "  4. 临时讨论组：不支持\n"
              << "  5. 禁言：不支持，所有成员都可以发群消息（原有的禁言名单冻结保留）\n"
              << "  6. 群资料：只有群主可以修改群名称、发布群公告\n";
}
