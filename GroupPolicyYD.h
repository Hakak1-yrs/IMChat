#pragma once
#include <memory>
#include <string>

// 群管理模式
enum class GroupModeYD { QQ, WECHAT };

class GroupYD;

// 群管理策略抽象类（策略模式）：
// 不同微X的“群理念”不同，把这些差异封装成策略对象，群在运行时可以更换策略，
// 从而在不改动群成员数据的前提下动态切换管理特色
class GroupPolicyYD {
public:
    virtual ~GroupPolicyYD() = default;

    virtual GroupModeYD mode() const = 0;
    virtual std::string modeName() const = 0;
    virtual bool allowApplyJoin() const = 0;   // 是否允许主动申请入群
    virtual bool allowSubGroup() const = 0;    // 是否允许创建临时讨论组（子群）
    virtual bool allowAdmin() const = 0;       // 是否有管理员制度
    virtual bool allowMute() const = 0;        // 是否有禁言制度
    virtual bool canInvite(const GroupYD& group, const std::string& op) const;   // 默认：群成员可推荐他人入群
    virtual bool canApprove(const GroupYD& group, const std::string& op) const = 0;
    virtual bool canKick(const GroupYD& group, const std::string& op, const std::string& target) const = 0;
    virtual bool canMute(const GroupYD& group, const std::string& op, const std::string& target) const = 0;
    virtual bool canEditInfo(const GroupYD& group, const std::string& op) const = 0;   // 修改群名称、群公告
    // 默认：群成员可以发言；有禁言制度时，被禁言的成员不能发言。
    // 没有禁言制度的模式下禁言名单被“冻结保留”，切换回来后重新生效
    virtual bool canSpeak(const GroupYD& group, const std::string& id) const;
    // 能否撤回别人的群消息（自己的消息 2 分钟内都能撤回）。默认与踢人的权限相同：
    // QQ 群模式群主可撤回任何成员、管理员可撤回普通成员；微信群模式只有群主可以
    virtual bool canRevoke(const GroupYD& group, const std::string& op, const std::string& sender) const;
    virtual void showFeatures() const = 0;

    // 简单工厂：根据模式创建对应的策略对象
    static std::unique_ptr<GroupPolicyYD> create(GroupModeYD mode);
    static std::string modeCode(GroupModeYD mode);
    static bool parseMode(const std::string& code, GroupModeYD& mode);
};
