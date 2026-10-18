#include "GroupYD.h"
#include <algorithm>
#include "UtilYD.h"

GroupYD::GroupYD(ServiceTypeYD service, int groupNo, const std::string& name,
                 const std::string& ownerId, GroupModeYD mode)
    : service_(service), groupNo_(groupNo), name_(name), ownerId_(ownerId),
      policy_(GroupPolicyYD::create(mode)) {
    if (ownerId_ == "-") ownerId_.clear();          // 文件中用 "-" 表示无主群
    if (!ownerId_.empty()) members_.push_back(ownerId_);   // 群主一定是群成员
}

ServiceTypeYD GroupYD::service() const { return service_; }

int GroupYD::groupNo() const { return groupNo_; }

const std::string& GroupYD::name() const { return name_; }

const std::string& GroupYD::ownerId() const { return ownerId_; }

const std::vector<std::string>& GroupYD::members() const { return members_; }

const std::vector<std::string>& GroupYD::admins() const { return admins_; }

const std::vector<std::string>& GroupYD::pending() const { return pending_; }

const std::vector<std::string>& GroupYD::muted() const { return muted_; }

const std::string& GroupYD::notice() const { return notice_; }

const std::vector<SubGroupYD>& GroupYD::subGroups() const { return subGroups_; }

const GroupPolicyYD& GroupYD::policy() const { return *policy_; }

bool GroupYD::isMember(const std::string& id) const { return UtilYD::contains(members_, id); }

bool GroupYD::isOwner(const std::string& id) const { return !ownerId_.empty() && id == ownerId_; }

bool GroupYD::hasOwner() const { return !ownerId_.empty(); }

bool GroupYD::isAdmin(const std::string& id) const { return UtilYD::contains(admins_, id); }

bool GroupYD::isMuted(const std::string& id) const { return UtilYD::contains(muted_, id); }

bool GroupYD::canSpeak(const std::string& id) const { return policy_->canSpeak(*this, id); }

bool GroupYD::applyJoin(const std::string& applicant, std::string& msg) {
    if (isMember(applicant)) {
        msg = "你已经是该群成员。";
        return false;
    }
    if (!hasOwner()) {   // 原群主注销后无人接任的群，第一个加入的人认领为群主
        ownerId_ = applicant;
        members_.push_back(applicant);
        UtilYD::removeValue(pending_, applicant);
        msg = "该群原群主已注销且无人接任，你已加入并成为新群主。";
        notify(GroupEventYD::CLAIMED, applicant);
        return true;
    }
    if (!policy_->allowApplyJoin()) {
        msg = "当前为" + policy_->modeName() + "，不能主动申请入群，只能由群成员推荐加入。";
        return false;
    }
    if (UtilYD::contains(pending_, applicant)) {
        msg = "你已提交过申请，请等待审批。";
        return false;
    }
    pending_.push_back(applicant);
    msg = "入群申请已提交，等待群主或管理员审批。";
    return true;
}

bool GroupYD::approve(const std::string& op, const std::string& applicant, bool accept, std::string& msg) {
    if (!policy_->canApprove(*this, op)) {
        msg = "当前为" + policy_->modeName() + "，你没有审批入群申请的权限。";
        return false;
    }
    if (!UtilYD::removeValue(pending_, applicant)) {
        msg = "没有该账号的入群申请。";
        return false;
    }
    if (accept) {
        members_.push_back(applicant);
        msg = "已同意 " + applicant + " 入群。";
        notify(GroupEventYD::APPROVED, op, applicant);
    } else {
        msg = "已拒绝 " + applicant + " 的入群申请。";
    }
    return true;
}

bool GroupYD::invite(const std::string& op, const std::string& invitee, std::string& msg) {
    if (!policy_->canInvite(*this, op)) {
        msg = "只有群成员才能推荐他人入群。";
        return false;
    }
    if (isMember(invitee)) {
        msg = "对方已经是群成员。";
        return false;
    }
    UtilYD::removeValue(pending_, invitee);
    members_.push_back(invitee);
    msg = "已推荐 " + invitee + " 加入群 " + std::to_string(groupNo_) + "。";
    notify(GroupEventYD::INVITED, op, invitee);
    return true;
}

bool GroupYD::quit(const std::string& member, std::string& msg) {
    if (!isMember(member)) {
        msg = "你不是该群成员。";
        return false;
    }
    if (isOwner(member)) {
        msg = "群主不能直接退群，请先把群主转让给其他成员。";
        return false;
    }
    removeMember(member);
    msg = "已退出群 " + std::to_string(groupNo_) + "。";
    notify(GroupEventYD::QUIT, member);
    return true;
}

bool GroupYD::kick(const std::string& op, const std::string& target, std::string& msg) {
    if (!isMember(target)) {
        msg = "对方不是群成员。";
        return false;
    }
    if (!policy_->canKick(*this, op, target)) {
        msg = "当前为" + policy_->modeName() + "，你没有踢出该成员的权限。";
        return false;
    }
    removeMember(target);
    msg = "已将 " + target + " 移出群聊。";
    notify(GroupEventYD::KICKED, op, target);
    return true;
}

bool GroupYD::setAdmin(const std::string& op, const std::string& target, bool on, std::string& msg) {
    if (!policy_->allowAdmin()) {
        msg = "当前为" + policy_->modeName() + "，没有管理员制度，仅群主拥有特权。";
        return false;
    }
    if (!isOwner(op)) {
        msg = "只有群主可以设置管理员。";
        return false;
    }
    if (!isMember(target) || isOwner(target)) {
        msg = "目标必须是群主以外的群成员。";
        return false;
    }
    if (on) {
        if (isAdmin(target)) {
            msg = "对方已经是管理员。";
            return false;
        }
        admins_.push_back(target);
        msg = "已将 " + target + " 设为管理员。";
        notify(GroupEventYD::ADMIN_SET, op, target);
    } else {
        if (!UtilYD::removeValue(admins_, target)) {
            msg = "对方不是管理员。";
            return false;
        }
        msg = "已取消 " + target + " 的管理员身份。";
        notify(GroupEventYD::ADMIN_UNSET, op, target);
    }
    return true;
}

bool GroupYD::createSubGroup(const std::string& op, const std::string& name,
                             const std::vector<std::string>& members, std::string& msg) {
    if (!policy_->allowSubGroup()) {
        msg = "当前为" + policy_->modeName() + "，不允许创建临时讨论组。";
        return false;
    }
    if (!isMember(op)) {
        msg = "只有群成员才能创建讨论组。";
        return false;
    }
    for (const SubGroupYD& s : subGroups_) {
        if (s.name() == name) {
            msg = "讨论组名称已存在。";
            return false;
        }
    }
    SubGroupYD sub(name);
    sub.addMember(op);
    std::vector<std::string> ignored;
    for (const std::string& m : members) {
        if (isMember(m)) sub.addMember(m);   // 讨论组成员只能来自本群
        else ignored.push_back(m);
    }
    subGroups_.push_back(sub);
    msg = "讨论组 " + name + " 创建成功，共 " + std::to_string(sub.members().size()) + " 人。";
    if (!ignored.empty()) msg += "（" + UtilYD::joinList(ignored) + " 不是本群成员，已忽略）";
    return true;
}

bool GroupYD::switchMode(const std::string& op, GroupModeYD mode, std::string& msg) {
    if (!isOwner(op)) {
        msg = "只有群主可以切换群管理模式。";
        return false;
    }
    if (policy_->mode() == mode) {
        msg = "当前已经是" + policy_->modeName() + "。";
        return false;
    }
    std::string oldName = policy_->modeName();
    policy_ = GroupPolicyYD::create(mode);   // 只替换管理策略，成员、管理员、讨论组数据保持不变
    msg = "已从" + oldName + "切换为" + policy_->modeName() + "。";
    notify(GroupEventYD::MODE_SWITCHED, op, policy_->modeName());
    return true;
}

bool GroupYD::transferOwner(const std::string& op, const std::string& newOwner, std::string& msg) {
    if (!isOwner(op)) {
        msg = "只有群主可以转让群。";
        return false;
    }
    if (newOwner == op || !isMember(newOwner)) {
        msg = "新群主必须是本群的其他成员。";
        return false;
    }
    UtilYD::removeValue(admins_, newOwner);   // 群主本身拥有全部权限，不再记为管理员
    UtilYD::removeValue(muted_, newOwner);    // 群主不能处于禁言状态
    ownerId_ = newOwner;
    msg = "群主已转让给 " + newOwner + "，你现在是普通成员。";
    notify(GroupEventYD::TRANSFERRED, op, newOwner);
    return true;
}

SubGroupYD* GroupYD::findSubGroup(const std::string& name) {
    for (SubGroupYD& s : subGroups_) {
        if (s.name() == name) return &s;
    }
    return nullptr;
}

bool GroupYD::addToSubGroup(const std::string& op, const std::string& subName, const std::string& member,
                            std::string& msg) {
    if (!policy_->allowSubGroup()) {
        msg = "当前为" + policy_->modeName() + "，讨论组已冻结，不能修改。";
        return false;
    }
    SubGroupYD* sub = findSubGroup(subName);
    if (!sub || !UtilYD::contains(sub->members(), op)) {
        msg = "只有讨论组成员才能拉人。";
        return false;
    }
    if (!isMember(member)) {
        msg = "讨论组成员只能来自本群。";
        return false;
    }
    if (!sub->addMember(member)) {
        msg = "对方已经在讨论组中。";
        return false;
    }
    msg = "已把 " + member + " 拉入讨论组 " + subName + "。";
    return true;
}

bool GroupYD::dissolveSubGroup(const std::string& op, const std::string& subName, std::string& msg) {
    if (!policy_->allowSubGroup()) {
        msg = "当前为" + policy_->modeName() + "，讨论组已冻结，不能修改。";
        return false;
    }
    for (auto it = subGroups_.begin(); it != subGroups_.end(); ++it) {
        if (it->name() != subName) continue;
        if (it->creator() != op && !isOwner(op)) {
            msg = "只有讨论组创建者或群主可以解散讨论组。";
            return false;
        }
        subGroups_.erase(it);
        msg = "讨论组 " + subName + " 已解散。";
        return true;
    }
    msg = "没有名为 " + subName + " 的讨论组。";
    return false;
}

bool GroupYD::rename(const std::string& op, const std::string& name, std::string& msg) {
    if (!policy_->canEditInfo(*this, op)) {
        msg = "当前为" + policy_->modeName() + "，你没有修改群名称的权限。";
        return false;
    }
    const std::string newName = UtilYD::toField(UtilYD::lineText(name));   // 群名在文件中是一个字段
    if (newName.empty()) {
        msg = "群名称不能为空。";
        return false;
    }
    msg = "群名称已由“" + name_ + "”改为“" + newName + "”。";
    name_ = newName;
    notify(GroupEventYD::RENAMED, op, newName);
    return true;
}

bool GroupYD::setNotice(const std::string& op, const std::string& notice, std::string& msg) {
    if (!policy_->canEditInfo(*this, op)) {
        msg = "当前为" + policy_->modeName() + "，你没有发布群公告的权限。";
        return false;
    }
    const std::string text = UtilYD::lineText(notice);
    if (UtilYD::charCount(text) > 150) {
        msg = "群公告太长（最多 150 个字）。";
        return false;
    }
    notice_ = text;
    msg = text.empty() ? "群公告已清除。" : "群公告已发布。";
    return true;
}

bool GroupYD::setMuted(const std::string& op, const std::string& target, bool on, std::string& msg) {
    if (!policy_->allowMute()) {
        msg = "当前为" + policy_->modeName() + "，没有禁言制度。";
        return false;
    }
    if (!isMember(target)) {
        msg = "对方不是群成员。";
        return false;
    }
    if (!policy_->canMute(*this, op, target)) {
        msg = "你没有禁言该成员的权限（群主可禁言任何成员，管理员只能禁言普通成员）。";
        return false;
    }
    if (on) {
        if (isMuted(target)) {
            msg = "对方已经处于禁言状态。";
            return false;
        }
        muted_.push_back(target);
        msg = "已将 " + target + " 禁言。";
        notify(GroupEventYD::MUTED, op, target);
    } else {
        if (!UtilYD::removeValue(muted_, target)) {
            msg = "对方没有被禁言。";
            return false;
        }
        msg = "已解除 " + target + " 的禁言。";
        notify(GroupEventYD::UNMUTED, op, target);
    }
    return true;
}

void GroupYD::removeAccount(const std::string& id) {
    UtilYD::removeValue(pending_, id);
    if (isOwner(id)) {
        std::string successor;
        if (!admins_.empty()) {
            successor = admins_.front();
        } else {
            for (const std::string& m : members_) {
                if (m != id) {
                    successor = m;
                    break;
                }
            }
        }
        ownerId_ = successor;   // 为空表示无人接任
        UtilYD::removeValue(admins_, successor);
        UtilYD::removeValue(muted_, successor);
    }
    removeMember(id);
}

std::string GroupYD::dataSnapshot() const {
    std::string text = "群主=" + (hasOwner() ? ownerId_ : "-") + "；成员=" + UtilYD::joinList(members_) +
                       "；管理员=" + UtilYD::joinList(admins_) + "；申请=" + UtilYD::joinList(pending_) +
                       "；禁言=" + UtilYD::joinList(muted_) + "；公告=" + notice_;
    for (const SubGroupYD& s : subGroups_) text += "；讨论组" + s.name() + "=" + UtilYD::joinList(s.members());
    return text;
}

int GroupYD::loadData(const std::vector<std::string>& members, const std::vector<std::string>& admins,
                      const std::vector<std::string>& pending, const std::vector<std::string>& muted) {
    int repaired = 0;
    if (hasOwner() && !UtilYD::contains(members, ownerId_)) ++repaired;   // 构造函数已把群主补进成员
    for (const std::string& m : members) {
        if (!isMember(m)) members_.push_back(m);
        else if (m != ownerId_) ++repaired;   // 重复的成员
    }
    for (const std::string& a : admins) {
        if (isMember(a) && !isOwner(a) && !isAdmin(a)) admins_.push_back(a);
        else ++repaired;
    }
    for (const std::string& p : pending) {
        if (!isMember(p) && !UtilYD::contains(pending_, p)) pending_.push_back(p);
        else ++repaired;
    }
    if (!hasOwner() && !members_.empty()) {   // 无主群只能是空群，否则群里的人谁也管理不了这个群
        ownerId_ = admins_.empty() ? members_.front() : admins_.front();
        UtilYD::removeValue(admins_, ownerId_);
        ++repaired;
    }
    for (const std::string& m : muted) {
        if (isMember(m) && !isOwner(m) && !isMuted(m)) muted_.push_back(m);
        else ++repaired;
    }
    return repaired;
}

void GroupYD::loadNotice(const std::string& notice) { notice_ = UtilYD::lineText(notice); }

void GroupYD::setListener(GroupListenerYD* listener) { listener_ = listener; }

void GroupYD::notify(GroupEventYD event, const std::string& actor, const std::string& target) {
    if (listener_) listener_->onGroupEvent(*this, event, actor, target);
}

bool GroupYD::loadSubGroup(const SubGroupYD& subGroup) {
    if (subGroup.members().empty() || findSubGroup(subGroup.name())) return false;
    subGroups_.push_back(subGroup);
    return true;
}

void GroupYD::removeMember(const std::string& id) {
    UtilYD::removeValue(members_, id);
    UtilYD::removeValue(admins_, id);
    UtilYD::removeValue(muted_, id);
    for (SubGroupYD& s : subGroups_) s.removeMember(id);
    // 成员全部离开的讨论组已没有意义，随之删除（讨论组成员必须是群成员，这里同步维护）
    subGroups_.erase(std::remove_if(subGroups_.begin(), subGroups_.end(),
                                    [](const SubGroupYD& s) { return s.members().empty(); }),
                     subGroups_.end());
}
