#pragma once
#include "GroupPolicy.h"

// 微信群管理策略：只能由群成员推荐加入；不允许子群；仅群主为特权账号；没有禁言制度
class WeChatGroupPolicy : public GroupPolicy {
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
