#include "GroupService.h"
#include "GroupManager.h"
#include "Platform.h"
#include "Util.h"

namespace {

const char* const kKindNames[] = { "join",   "approve",  "invite", "quit",    "kick",   "admin",  "mute",   "transfer",
                                   "rename", "notice",   "mode",   "subNew",  "subAdd", "subDissolve", "dissolve" };

std::string nicknameOf(Platform& platform, ServiceType type, const std::string& id) {
    ServiceBase* account = platform.findAccount(type, id);
    return account ? account->nickname() : id;
}

}  // namespace

std::string GroupCommand::kindName(Kind kind) { return kKindNames[kind]; }

bool GroupCommand::parseKind(const std::string& name, Kind& kind) {
    for (int i = 0; i <= DISSOLVE; ++i) {
        if (name == kKindNames[i]) {
            kind = static_cast<Kind>(i);
            return true;
        }
    }
    return false;
}

// ======================== 查询 ========================

GroupInfo GroupService::describe(Platform& platform, const ServiceBase& viewer, int groupNo) {
    GroupInfo info;
    info.groupNo = groupNo;
    const Group* g = platform.groups().find(viewer.type(), groupNo);
    if (!g) return info;
    const GroupPolicy& policy = g->policy();
    const std::string& me = viewer.id();
    info.exists = true;
    info.name = g->name();
    info.mode = GroupPolicy::modeCode(policy.mode());
    info.modeName = policy.modeName();
    info.owner = g->ownerId();
    info.notice = g->notice();
    info.preset = GroupManager::isPreset(groupNo);
    info.features = Util::captureLines([&] { policy.showFeatures(); });

    info.member = g->isMember(me);
    info.isOwner = g->isOwner(me);
    info.isAdmin = policy.allowAdmin() && g->isAdmin(me);
    info.applied = Util::contains(g->pending(), me);
    info.canEditInfo = policy.canEditInfo(*g, me);
    info.allowApplyJoin = policy.allowApplyJoin();
    info.allowSubGroup = policy.allowSubGroup();
    info.allowAdmin = policy.allowAdmin();
    info.allowMute = policy.allowMute();
    info.canApprove = policy.canApprove(*g, me);
    info.canDissolve = info.isOwner && !info.preset;

    for (const std::string& id : g->members()) {
        GroupMemberInfo m;
        m.id = id;
        m.nickname = nicknameOf(platform, viewer.type(), id);
        m.owner = g->isOwner(id);
        m.admin = policy.allowAdmin() && g->isAdmin(id);
        m.muted = policy.allowMute() && g->isMuted(id);
        if (info.member && id != me) {
            m.canSetAdmin = info.isOwner && policy.allowAdmin() && !m.owner;
            m.canMute = policy.allowMute() && policy.canMute(*g, me, id);
            m.canTransfer = info.isOwner;
            m.canKick = policy.canKick(*g, me, id);
        }
        info.members.push_back(m);
    }
    for (const std::string& id : g->pending()) {
        GroupMemberInfo m;
        m.id = id;
        m.nickname = nicknameOf(platform, viewer.type(), id);
        info.pending.push_back(m);
    }
    for (const SubGroup& s : g->subGroups()) {
        SubGroupInfo sub;
        sub.name = s.name();
        sub.creator = s.creator();
        sub.members = s.members();
        sub.canAdd = policy.allowSubGroup() && Util::contains(s.members(), me);
        sub.canDissolve = policy.allowSubGroup() && (s.creator() == me || info.isOwner);
        info.subGroups.push_back(sub);
    }
    if (!policy.allowAdmin()) info.frozenAdmins = static_cast<int>(g->admins().size());
    if (!policy.allowMute()) info.frozenMuted = static_cast<int>(g->muted().size());
    return info;
}

std::vector<GroupBrief> GroupService::list(Platform& platform, const ServiceBase& viewer) {
    std::vector<GroupBrief> result;
    for (const Group* g : platform.groups().groupsOfService(viewer.type())) {
        GroupBrief brief;
        brief.groupNo = g->groupNo();
        brief.name = g->name();
        brief.modeName = g->policy().modeName();
        brief.owner = g->ownerId();
        brief.ownerName = g->hasOwner() ? nicknameOf(platform, viewer.type(), g->ownerId()) : "";
        brief.memberCount = static_cast<int>(g->members().size());
        brief.joined = g->isMember(viewer.id());
        brief.applied = Util::contains(g->pending(), viewer.id());
        brief.allowApplyJoin = g->policy().allowApplyJoin();
        result.push_back(brief);
    }
    return result;
}

// ======================== 执行 ========================

int GroupService::create(Platform& platform, const ServiceBase& me, const std::string& name, std::string& msg) {
    Group* g = platform.createGroup(me, name, msg);
    return g ? g->groupNo() : 0;
}

bool GroupService::execute(Platform& platform, const ServiceBase& me, const GroupCommand& command, std::string& msg) {
    if (command.kind == GroupCommand::DISSOLVE) return platform.dissolveGroup(me, command.groupNo, msg);
    Group* g = platform.groups().find(me.type(), command.groupNo);
    if (!g) {
        msg = "群 " + std::to_string(command.groupNo) + " 不存在或已解散。";
        return false;
    }
    const std::string& op = me.id();
    const std::string& target = command.target;
    switch (command.kind) {
    case GroupCommand::JOIN: return g->applyJoin(op, msg);
    case GroupCommand::APPROVE: return g->approve(op, target, command.on, msg);
    case GroupCommand::INVITE:
        // 只能推荐自己的好友（界面只列出好友；联网时请求可能被伪造，这里再检查一遍）
        if (!platform.findAccount(me.type(), target) || !me.friends().contains(target)) {
            msg = target + " 不是你的好友，只能推荐好友入群。";
            return false;
        }
        return g->invite(op, target, msg);
    case GroupCommand::QUIT: return g->quit(op, msg);
    case GroupCommand::KICK: return g->kick(op, target, msg);
    case GroupCommand::SET_ADMIN: return g->setAdmin(op, target, command.on, msg);
    case GroupCommand::SET_MUTED: return g->setMuted(op, target, command.on, msg);
    case GroupCommand::TRANSFER: return g->transferOwner(op, target, msg);
    case GroupCommand::RENAME: return g->rename(op, command.text, msg);
    case GroupCommand::SET_NOTICE:
        if (!g->setNotice(op, command.text, msg)) return false;
        // 发布（不是清除）公告时同时作为一条群消息发出，群成员会收到未读提醒
        if (!g->notice().empty()) platform.messages().postGroup(me.type(), g->groupNo(), op, "【群公告】" + g->notice());
        return true;
    case GroupCommand::SWITCH_MODE: {
        GroupMode mode = g->policy().mode() == GroupMode::QQ ? GroupMode::WECHAT : GroupMode::QQ;
        if (!command.text.empty() && !GroupPolicy::parseMode(command.text, mode)) {
            msg = "未知的管理模式：" + command.text;
            return false;
        }
        // 切换前后各取一次完整快照，逐项比对而不只是比人数，证明只换了规则、成员数据没有受影响
        const std::string before = g->dataSnapshot();
        if (!g->switchMode(op, mode, msg)) return false;
        msg += "\n切换前后逐项比对：群主、" + std::to_string(g->members().size()) + " 名成员、" + std::to_string(g->admins().size()) +
               " 条管理员记录、" + std::to_string(g->pending().size()) + " 条入群申请、" + std::to_string(g->muted().size()) +
               " 条禁言记录、群公告、" + std::to_string(g->subGroups().size()) + " 个讨论组——" +
               (g->dataSnapshot() == before ? "完全一致，群成员数据未受影响。" : "出现差异，请检查！");
        for (const std::string& line : Util::captureLines([&] { g->policy().showFeatures(); })) msg += "\n" + line;
        return true;
    }
    case GroupCommand::NEW_SUB_GROUP: {
        const std::string name = Util::toField(Util::lineText(command.text));
        if (name.empty()) {
            msg = "讨论组名称不能为空。";
            return false;
        }
        return g->createSubGroup(op, name, command.members, msg);
    }
    case GroupCommand::ADD_TO_SUB_GROUP: {
        if (command.members.empty()) {
            msg = "请至少选择一位成员。";
            return false;
        }
        bool any = false;
        msg.clear();
        for (const std::string& id : command.members) {
            std::string result;
            any = g->addToSubGroup(op, target, id, result) || any;
            msg += (msg.empty() ? "" : "；") + result;
        }
        return any;
    }
    case GroupCommand::DISSOLVE_SUB_GROUP: return g->dissolveSubGroup(op, target, msg);
    case GroupCommand::DISSOLVE: break;   // 已在前面处理
    }
    msg = "不支持的群操作。";
    return false;
}
