#pragma once
#include "GroupPolicy.h"

// QQ 群管理策略：可申请加入；可建临时讨论组；以群主为核心的管理员制度；群主、管理员可以禁言成员
class QQGroupPolicy : public GroupPolicy {
public:
    GroupMode mode() const override;
    std::string modeName() const override;
    bool allowApplyJoin() const override;
    bool allowSubGroup() const override;
    bool allowAdmin() const override;
    bool allowMute() const override;
    bool canApprove(const Group& group, const std::string& op) const override;
    bool canKick(const Group& group, const std::string& op, const std::string& target) const override;
    bool canMute(const Group& group, const std::string& op, const std::string& target) const override;
    bool canEditInfo(const Group& group, const std::string& op) const override;
    void showFeatures() const override;
};
