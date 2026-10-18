#include "QQGroupPolicyYD.h"
#include <iostream>
#include "GroupYD.h"

GroupModeYD QQGroupPolicyYD::mode() const { return GroupModeYD::QQ; }

std::string QQGroupPolicyYD::modeName() const { return "QQ群模式"; }

bool QQGroupPolicyYD::allowApplyJoin() const { return true; }

bool QQGroupPolicyYD::allowSubGroup() const { return true; }

bool QQGroupPolicyYD::allowAdmin() const { return true; }

bool QQGroupPolicyYD::allowMute() const { return true; }

bool QQGroupPolicyYD::canApprove(const GroupYD& group, const std::string& op) const {
    return group.isOwner(op) || group.isAdmin(op);
}

bool QQGroupPolicyYD::canKick(const GroupYD& group, const std::string& op, const std::string& target) const {
    if (op == target || !group.isMember(target)) return false;
    if (group.isOwner(op)) return true;                                          // 群主可以踢任何人
    if (group.isAdmin(op)) return !group.isOwner(target) && !group.isAdmin(target); // 管理员只能踢普通成员
    return false;
}

// 禁言与踢人的权限层级相同：群主可以禁言任何成员，管理员只能禁言普通成员
bool QQGroupPolicyYD::canMute(const GroupYD& group, const std::string& op, const std::string& target) const {
    return canKick(group, op, target);
}

bool QQGroupPolicyYD::canEditInfo(const GroupYD& group, const std::string& op) const {
    return group.isOwner(op) || group.isAdmin(op);
}

void QQGroupPolicyYD::showFeatures() const {
    std::cout << "  【QQ群特色】\n"
              << "  1. 入群方式：用户可以主动申请加入，由群主或管理员审批；群成员也可以邀请好友\n"
              << "  2. 管理制度：以群主为核心，群主可以设置多名管理员\n"
              << "  3. 权限：群主可踢出任何成员；管理员可审批申请、踢出普通成员\n"
              << "  4. 临时讨论组：群成员可以从群里拉人创建临时讨论组（子群）\n"
              << "  5. 禁言：群主可禁言任何成员，管理员可禁言普通成员；被禁言的成员不能发群消息\n"
              << "  6. 群资料：群主和管理员都可以修改群名称、发布群公告\n";
}
