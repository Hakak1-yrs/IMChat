#pragma once
#include <memory>
#include <string>
#include <vector>
#include "GroupListenerYD.h"
#include "GroupPolicyYD.h"
#include "ServiceTypeYD.h"
#include "SubGroupYD.h"

// 群：保存群的基本信息和成员数据；所有权限判断都委托给当前的管理策略对象。
// 切换管理模式时只替换策略对象，成员、管理员、讨论组等数据保持不变
class GroupYD {
public:
    GroupYD(ServiceTypeYD service, int groupNo, const std::string& name,
            const std::string& ownerId, GroupModeYD mode);

    ServiceTypeYD service() const;
    int groupNo() const;
    const std::string& name() const;
    const std::string& ownerId() const;
    const std::vector<std::string>& members() const;
    const std::vector<std::string>& admins() const;
    const std::vector<std::string>& pending() const;     // 待审批的入群申请
    const std::vector<std::string>& muted() const;       // 被禁言的成员
    const std::string& notice() const;                   // 群公告，为空表示没有公告
    const std::vector<SubGroupYD>& subGroups() const;
    const GroupPolicyYD& policy() const;

    bool isMember(const std::string& id) const;
    bool isOwner(const std::string& id) const;
    bool isAdmin(const std::string& id) const;
    bool isMuted(const std::string& id) const;            // 是否在禁言名单中（是否生效由当前策略决定）
    bool canSpeak(const std::string& id) const;           // 当前能否在群里发言

    // ---- 群操作接口：成功返回 true，msg 中给出提示信息 ----
    bool applyJoin(const std::string& applicant, std::string& msg);
    bool approve(const std::string& op, const std::string& applicant, bool accept, std::string& msg);
    bool invite(const std::string& op, const std::string& invitee, std::string& msg);
    bool quit(const std::string& member, std::string& msg);
    bool kick(const std::string& op, const std::string& target, std::string& msg);
    bool setAdmin(const std::string& op, const std::string& target, bool on, std::string& msg);
    bool createSubGroup(const std::string& op, const std::string& name,
                        const std::vector<std::string>& members, std::string& msg);
    bool switchMode(const std::string& op, GroupModeYD mode, std::string& msg);
    bool transferOwner(const std::string& op, const std::string& newOwner, std::string& msg);
    bool addToSubGroup(const std::string& op, const std::string& subName, const std::string& member, std::string& msg);
    bool dissolveSubGroup(const std::string& op, const std::string& subName, std::string& msg);
    bool rename(const std::string& op, const std::string& name, std::string& msg);
    bool setNotice(const std::string& op, const std::string& notice, std::string& msg);   // notice 为空表示清除公告
    bool setMuted(const std::string& op, const std::string& target, bool on, std::string& msg);

    // 账号被关闭或注销时调用：移除该账号的全部群数据。若他是群主，
    // 优先由第一位管理员接任，其次由最早入群的成员接任；没人可接任时群变为“无主群”
    void removeAccount(const std::string& id);
    bool hasOwner() const;

    // 群主、成员、管理员、入群申请、禁言名单、群公告和讨论组拼成的一段文字。
    // 切换管理模式前后各取一次并比较，用来证明“群成员数据不受伤害”
    std::string dataSnapshot() const;

    // ---- 从文件加载数据时使用（不做权限检查）----
    // 返回自动修正的数据处数：重复成员、不是成员的管理员、已是成员的申请人、不是成员（或是群主）的
    // 禁言记录被丢弃，群主不在成员列表中时补回，无主群中仍有成员时按接任规则补选群主
    int loadData(const std::vector<std::string>& members, const std::vector<std::string>& admins,
                 const std::vector<std::string>& pending, const std::vector<std::string>& muted);
    void loadNotice(const std::string& notice);
    bool loadSubGroup(const SubGroupYD& subGroup);   // 讨论组为空或与已有讨论组重名时拒绝，返回 false

    void setListener(GroupListenerYD* listener);   // 群操作成功后通知它（加载数据时不通知）

private:
    void removeMember(const std::string& id);   // 同时清除其管理员身份和讨论组成员身份，删除变空的讨论组
    void notify(GroupEventYD event, const std::string& actor, const std::string& target = "");
    SubGroupYD* findSubGroup(const std::string& name);

    ServiceTypeYD service_;
    int groupNo_;
    std::string name_;
    std::string ownerId_;
    std::vector<std::string> members_;
    std::vector<std::string> admins_;
    std::vector<std::string> pending_;
    std::vector<std::string> muted_;
    std::string notice_;
    std::vector<SubGroupYD> subGroups_;
    std::unique_ptr<GroupPolicyYD> policy_;
    GroupListenerYD* listener_ = nullptr;
};
