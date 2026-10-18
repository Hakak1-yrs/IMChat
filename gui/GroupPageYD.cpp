#include "GroupPageYD.h"
#include "ConversationYD.h"
#include "DialogYD.h"
#include "GroupManagerYD.h"
#include "GuiTextYD.h"
#include "GuiWindowYD.h"
#include "MainViewYD.h"
#include "UtilYD.h"

namespace {
const unsigned kOwnerColor = 0x2B7FFF;
const unsigned kAdminColor = 0xFF7A1A;
const unsigned kGrayColor = 0x86909C;
}  // namespace

GroupPageYD::GroupPageYD(GuiWindowYD& window, int groupNo) : PageYD(window), groupNo_(groupNo) {}

std::string GroupPageYD::key() const { return "G:" + std::to_string(groupNo_); }

GroupYD* GroupPageYD::group() { return platform().groups().find(me().type(), groupNo_); }

std::wstring GroupPageYD::name(const std::string& id) { return GuiTextYD::wide(platform().displayName(me().type(), id)); }

// ======================== 页面内容 ========================

void GroupPageYD::build(PageModelYD& model) {
    GroupYD* g = group();
    if (!g) {
        model.title = L"群 " + std::to_wstring(groupNo_);
        model.note(L"该群已解散。");
        return;
    }
    const std::string meId = me().id();
    const GroupPolicyYD& policy = g->policy();
    const bool member = g->isMember(meId), owner = g->isOwner(meId);
    const std::string no = std::to_string(groupNo_);
    model.title = GuiTextYD::wide(g->name());
    model.subtitle = L"群号 " + std::to_wstring(groupNo_) + L" · " + GuiTextYD::wide(policy.modeName()) + L" · " +
                     std::to_wstring(g->members().size()) + L" 人";
    if (member) {
        model.headerButtons.push_back(PageModelYD::button(L"实时群聊室", ACT_ROOM));
        model.headerButtons.push_back(PageModelYD::button(L"进入群聊", ACT_CHAT, "", PageModelYD::PRIMARY));
    }

    // ---- 群资料 ----
    model.section(L"群资料");
    PageModelYD::RowYD& nameRow = model.info(L"群名称", GuiTextYD::wide(g->name()));
    if (policy.canEditInfo(*g, meId)) nameRow.buttons.push_back(PageModelYD::button(L"修改", ACT_RENAME));
    model.info(L"群号", std::to_wstring(groupNo_) + (GroupManagerYD::isPreset(groupNo_) ? L"（平台预置群）" : L""));
    PageModelYD::RowYD& modeRow = model.info(L"管理模式", GuiTextYD::wide(policy.modeName()));
    if (owner) {
        modeRow.buttons.push_back(PageModelYD::button(policy.mode() == GroupModeYD::QQ ? L"切换为微信群模式" : L"切换为QQ群模式", ACT_SWITCH));
    }
    model.info(L"群主", g->hasOwner() ? name(g->ownerId()) : L"无（第一个申请加入的人将成为新群主）");
    const std::wstring role = owner                                           ? L"群主"
                              : (policy.allowAdmin() && g->isAdmin(meId))     ? L"管理员"
                              : member                                         ? L"普通成员"
                                                                               : L"非成员";
    model.info(L"我的身份", role);
    PageModelYD::RowYD& noticeRow = model.info(L"群公告", g->notice().empty() ? L"无" : GuiTextYD::wide(g->notice()));
    if (policy.canEditInfo(*g, meId)) {
        noticeRow.buttons.push_back(PageModelYD::button(L"编辑", ACT_NOTICE));
        if (!g->notice().empty()) noticeRow.buttons.push_back(PageModelYD::button(L"清除", ACT_CLEAR_NOTICE));
    }

    // ---- 群特色：由当前管理策略的虚函数 showFeatures() 输出 ----
    model.section(L"群特色（随管理模式变化）");
    std::wstring features;
    for (const std::string& line : GuiTextYD::capture([&] { policy.showFeatures(); })) {
        if (line.find("【") == 0) continue;   // 标题行已在小标题中体现
        features += (features.empty() ? L"" : L"\n") + GuiTextYD::wide(line);
    }
    model.text(features);

    // ---- 操作 ----
    std::vector<PageModelYD::ButtonYD> actions;
    if (!member) {
        actions.push_back(PageModelYD::button(g->hasOwner() ? L"申请加入" : L"认领本群", ACT_JOIN, "", PageModelYD::PRIMARY));
    } else {
        actions.push_back(PageModelYD::button(L"推荐好友入群", ACT_INVITE));
        if (policy.allowSubGroup()) actions.push_back(PageModelYD::button(L"新建讨论组", ACT_NEW_SUB));
        actions.push_back(PageModelYD::button(L"退出群", ACT_QUIT, "", PageModelYD::DANGER));
        if (owner && !GroupManagerYD::isPreset(groupNo_)) actions.push_back(PageModelYD::button(L"解散群", ACT_DISSOLVE, "", PageModelYD::DANGER));
    }
    model.section(L"操作");
    model.actions(actions);
    if (!member && !policy.allowApplyJoin() && g->hasOwner()) model.note(L"当前为" + GuiTextYD::wide(policy.modeName()) + L"，不能主动申请，只能由群成员推荐加入。");

    // ---- 成员：按钮按当前策略的权限出现 ----
    model.section(L"成员 · " + std::to_wstring(g->members().size()));
    for (const std::string& id : g->members()) {
        ServiceBaseYD* acc = platform().findAccount(me().type(), id);
        PageModelYD::RowYD& row = model.person(id, GuiTextYD::wide(acc ? acc->nickname() : id), name(id), GuiTextYD::wide(id));
        if (g->isOwner(id)) row.tags.push_back({ L"群主", kOwnerColor });
        else if (policy.allowAdmin() && g->isAdmin(id)) row.tags.push_back({ L"管理员", kAdminColor });
        if (policy.allowMute() && g->isMuted(id)) row.tags.push_back({ L"禁言", kGrayColor });
        if (id == meId) row.tags.push_back({ L"我", kGrayColor });
        if (!member || id == meId) continue;
        if (owner && policy.allowAdmin() && !g->isOwner(id))
            row.buttons.push_back(g->isAdmin(id) ? PageModelYD::button(L"取消管理员", ACT_ADMIN_OFF, id)
                                                 : PageModelYD::button(L"设为管理员", ACT_ADMIN_ON, id));
        if (policy.allowMute() && policy.canMute(*g, meId, id))
            row.buttons.push_back(g->isMuted(id) ? PageModelYD::button(L"解除禁言", ACT_MUTE_OFF, id)
                                                 : PageModelYD::button(L"禁言", ACT_MUTE_ON, id));
        if (owner) row.buttons.push_back(PageModelYD::button(L"转让群主", ACT_TRANSFER, id));
        if (policy.canKick(*g, meId, id)) row.buttons.push_back(PageModelYD::button(L"踢出", ACT_KICK, id, PageModelYD::DANGER));
    }
    if (!policy.allowAdmin() && !g->admins().empty())
        model.note(L"原有 " + std::to_wstring(g->admins().size()) + L" 名管理员的数据已保留，当前模式下仅群主拥有特权。");
    if (!policy.allowMute() && !g->muted().empty())
        model.note(L"原有 " + std::to_wstring(g->muted().size()) + L" 条禁言记录已保留，当前模式下不生效。");

    // ---- 入群申请（QQ 群模式）----
    if (policy.allowApplyJoin()) {
        model.section(L"入群申请 · " + std::to_wstring(g->pending().size()));
        const bool canApprove = policy.canApprove(*g, meId);
        if (g->pending().empty()) model.note(L"暂无待审批的申请");
        for (const std::string& id : g->pending()) {
            ServiceBaseYD* acc = platform().findAccount(me().type(), id);
            PageModelYD::RowYD& row = model.person(id, GuiTextYD::wide(acc ? acc->nickname() : id), name(id), L"申请加入本群");
            if (canApprove) {
                row.buttons.push_back(PageModelYD::button(L"同意", ACT_APPROVE, id, PageModelYD::PRIMARY));
                row.buttons.push_back(PageModelYD::button(L"拒绝", ACT_REJECT, id));
            }
        }
        if (!g->pending().empty() && !canApprove) model.note(L"只有群主或管理员可以审批入群申请。");
    }

    // ---- 临时讨论组（QQ 群模式）----
    model.section(L"临时讨论组 · " + std::to_wstring(g->subGroups().size()));
    if (!policy.allowSubGroup()) {
        model.note(g->subGroups().empty() ? L"当前模式不支持临时讨论组。"
                                          : L"原有 " + std::to_wstring(g->subGroups().size()) + L" 个讨论组的数据已保留，切换回QQ群模式后可用。");
    } else if (g->subGroups().empty()) {
        model.note(L"暂无讨论组");
    }
    for (const SubGroupYD& s : g->subGroups()) {
        std::wstring members;
        for (const std::string& id : s.members()) members += (members.empty() ? L"" : L"、") + name(id);
        PageModelYD::RowYD& row = model.person("S" + s.name(), GuiTextYD::wide(s.name()), GuiTextYD::wide(s.name()),
                                               std::to_wstring(s.members().size()) + L" 人：" + members, true);
        if (!policy.allowSubGroup()) continue;
        if (UtilYD::contains(s.members(), meId)) row.buttons.push_back(PageModelYD::button(L"拉人", ACT_SUB_ADD, s.name()));
        if (s.creator() == meId || owner) row.buttons.push_back(PageModelYD::button(L"解散", ACT_SUB_DISSOLVE, s.name(), PageModelYD::DANGER));
    }
}

// ======================== 操作 ========================

void GroupPageYD::report(const std::string& msg) {
    toast(GuiTextYD::wide(msg));
    refresh();
}

void GroupPageYD::confirm(const std::wstring& title, const std::wstring& text, const std::wstring& button, bool danger,
                          std::function<std::string()> operation) {
    auto dialog = std::make_unique<DialogYD>(window_, title);
    dialog->message(text).primary(button, [this, operation](DialogYD&) {
        report(operation());
        return true;
    }, danger);
    window_.openDialog(std::move(dialog));
}

void GroupPageYD::onAction(int action, const std::string& arg) {
    GroupYD* g = group();
    if (!g) return;
    const std::string meId = me().id();
    std::string msg;
    switch (action) {
    case ACT_CHAT: main().openChat(ConversationYD::groupChat(groupNo_)); return;
    case ACT_ROOM: main().showRoom(groupNo_); return;
    case ACT_JOIN:
        g->applyJoin(meId, msg);
        report(msg);
        return;
    case ACT_RENAME: {
        auto dialog = std::make_unique<DialogYD>(window_, L"修改群名称");
        dialog->field(L"新群名称", GuiTextYD::wide(g->name())).primary(L"保存", [this](DialogYD& d) {
            std::string result;
            GroupYD* grp = group();
            if (!grp || !grp->rename(me().id(), d.text(0), result)) {
                d.setError(GuiTextYD::wide(result));
                return false;
            }
            report(result);
            return true;
        });
        window_.openDialog(std::move(dialog));
        return;
    }
    case ACT_NOTICE: editNotice(); return;
    case ACT_CLEAR_NOTICE:
        confirm(L"清除群公告", L"确认清除群公告？", L"清除", true, [this, meId] {
            std::string result;
            if (GroupYD* grp = group()) grp->setNotice(meId, "", result);
            return result;
        });
        return;
    case ACT_SWITCH: switchMode(); return;
    case ACT_INVITE: inviteFriend(); return;
    case ACT_QUIT:
        confirm(L"退出群", L"确认退出群 " + GuiTextYD::wide(g->name()) + L"？", L"退出", true, [this, meId] {
            std::string result;
            if (GroupYD* grp = group()) grp->quit(meId, result);
            return result;
        });
        return;
    case ACT_DISSOLVE:
        if (main().roomActive(groupNo_)) {
            window_.info(L"解散群", L"本群的实时群聊室正在进行，请先关闭群聊室。");
            return;
        }
        confirm(L"解散群", L"解散后成员、讨论组和群聊记录都将删除，无法恢复。确认解散群 " + std::to_wstring(groupNo_) + L"（" +
                               GuiTextYD::wide(g->name()) + L"）？",
                L"解散", true, [this] {
                    std::string result;
                    if (platform().dissolveGroup(me(), groupNo_, result)) main().showSquare();
                    return result;
                });
        return;
    case ACT_NEW_SUB: createSubGroup(); return;
    case ACT_SUB_ADD: addToSubGroup(arg); return;
    case ACT_SUB_DISSOLVE:
        confirm(L"解散讨论组", L"确认解散讨论组 " + GuiTextYD::wide(arg) + L"？", L"解散", true, [this, meId, arg] {
            std::string result;
            if (GroupYD* grp = group()) grp->dissolveSubGroup(meId, arg, result);
            return result;
        });
        return;
    case ACT_ADMIN_ON:
    case ACT_ADMIN_OFF:
        g->setAdmin(meId, arg, action == ACT_ADMIN_ON, msg);
        report(msg);
        return;
    case ACT_MUTE_ON:
    case ACT_MUTE_OFF:
        g->setMuted(meId, arg, action == ACT_MUTE_ON, msg);
        report(msg);
        return;
    case ACT_KICK:
        confirm(L"踢出成员", L"确认把 " + name(arg) + L" 移出本群？", L"踢出", true, [this, meId, arg] {
            std::string result;
            if (GroupYD* grp = group()) grp->kick(meId, arg, result);
            return result;
        });
        return;
    case ACT_TRANSFER:
        confirm(L"转让群主", L"确认把群主转让给 " + name(arg) + L"？转让后你将成为普通成员。", L"转让", false, [this, meId, arg] {
            std::string result;
            if (GroupYD* grp = group()) grp->transferOwner(meId, arg, result);
            return result;
        });
        return;
    case ACT_APPROVE:
    case ACT_REJECT:
        g->approve(meId, arg, action == ACT_APPROVE, msg);
        report(msg);
        return;
    }
}

void GroupPageYD::editNotice() {
    GroupYD* g = group();
    auto dialog = std::make_unique<DialogYD>(window_, L"发布群公告");
    dialog->message(L"发布后会同时作为一条群消息发出，群成员会收到未读提醒。")
        .field(L"公告内容", GuiTextYD::wide(g->notice()), L"最多 150 个汉字")
        .primary(L"发布", [this](DialogYD& d) {
            GroupYD* grp = group();
            const std::string text = d.text(0);
            if (text.empty()) {
                d.setError(L"公告内容不能为空（清除公告请用“清除”按钮）。");
                return false;
            }
            std::string result;
            if (!grp || !grp->setNotice(me().id(), text, result)) {
                d.setError(GuiTextYD::wide(result));
                return false;
            }
            platform().messages().postGroup(grp->service(), grp->groupNo(), me().id(), "【群公告】" + grp->notice());
            report(result);
            return true;
        });
    window_.openDialog(std::move(dialog));
}

void GroupPageYD::switchMode() {
    GroupYD* g = group();
    const GroupModeYD target = g->policy().mode() == GroupModeYD::QQ ? GroupModeYD::WECHAT : GroupModeYD::QQ;
    const std::wstring targetName = target == GroupModeYD::QQ ? L"QQ群模式" : L"微信群模式";
    auto dialog = std::make_unique<DialogYD>(window_, L"切换群管理模式");
    dialog->message(L"把群 " + std::to_wstring(groupNo_) + L" 从" + GuiTextYD::wide(g->policy().modeName()) + L"切换为" + targetName +
                    L"？只更换管理规则，群主、成员、管理员、禁言记录、讨论组等数据保持不变。")
        .primary(L"切换", [this, target](DialogYD&) {
            GroupYD* grp = group();
            if (!grp) return true;
            // 切换前后各取一次完整快照，逐项比对而不只是比人数
            const std::string before = grp->dataSnapshot();
            std::string result;
            if (!grp->switchMode(me().id(), target, result)) {
                window_.info(L"切换群管理模式", GuiTextYD::wide(result));
                return true;
            }
            std::vector<std::wstring> lines{ GuiTextYD::wide(result) };
            lines.push_back(L"切换前后逐项比对：群主、" + std::to_wstring(grp->members().size()) + L" 名成员、" +
                            std::to_wstring(grp->admins().size()) + L" 条管理员记录、" + std::to_wstring(grp->pending().size()) +
                            L" 条入群申请、" + std::to_wstring(grp->muted().size()) + L" 条禁言记录、群公告、" +
                            std::to_wstring(grp->subGroups().size()) + L" 个讨论组——" +
                            (grp->dataSnapshot() == before ? L"完全一致，群成员数据未受影响。" : L"出现差异，请检查！"));
            for (const std::string& line : GuiTextYD::capture([&] { grp->policy().showFeatures(); })) lines.push_back(GuiTextYD::wide(line));
            auto done = std::make_unique<DialogYD>(window_, L"已切换为" + GuiTextYD::wide(grp->policy().modeName()));
            done->lines(lines).cancelText(L"知道了");
            window_.openDialog(std::move(done));
            refresh();
            return true;
        });
    window_.openDialog(std::move(dialog));
}

void GroupPageYD::inviteFriend() {
    GroupYD* g = group();
    std::vector<std::string> candidates;
    std::vector<std::wstring> names;
    for (const FriendYD& f : me().friends().all()) {
        if (g->isMember(f.id())) continue;
        candidates.push_back(f.id());
        names.push_back(name(f.id()));
    }
    if (candidates.empty()) {
        window_.info(L"推荐好友入群", L"没有可推荐的好友（好友都已在群中，或你还没有好友）。");
        return;
    }
    auto dialog = std::make_unique<DialogYD>(window_, L"推荐好友入群");
    dialog->pick(L"选择好友", names, false).primary(L"推荐", [this, candidates](DialogYD& d) {
        std::vector<size_t> picked = d.picked();
        if (picked.empty()) {
            d.setError(L"请选择一位好友。");
            return false;
        }
        std::string result;
        GroupYD* grp = group();
        if (!grp || !grp->invite(me().id(), candidates[picked[0]], result)) {
            d.setError(GuiTextYD::wide(result));
            return false;
        }
        report(result);
        return true;
    });
    window_.openDialog(std::move(dialog));
}

void GroupPageYD::createSubGroup() {
    GroupYD* g = group();
    std::vector<std::string> candidates;
    std::vector<std::wstring> names;
    for (const std::string& id : g->members()) {
        if (id == me().id()) continue;
        candidates.push_back(id);
        names.push_back(name(id));
    }
    auto dialog = std::make_unique<DialogYD>(window_, L"新建临时讨论组");
    dialog->field(L"讨论组名称").pick(L"从群成员中拉人（可多选）", names, true).primary(L"创建", [this, candidates](DialogYD& d) {
        const std::string subName = UtilYD::toField(d.text(0));
        if (subName.empty()) {
            d.setError(L"讨论组名称不能为空。");
            return false;
        }
        std::vector<std::string> members;
        for (size_t i : d.picked()) members.push_back(candidates[i]);
        std::string result;
        GroupYD* grp = group();
        if (!grp || !grp->createSubGroup(me().id(), subName, members, result)) {
            d.setError(GuiTextYD::wide(result));
            return false;
        }
        report(result);
        return true;
    });
    window_.openDialog(std::move(dialog));
}

void GroupPageYD::addToSubGroup(const std::string& subName) {
    GroupYD* g = group();
    const SubGroupYD* sub = nullptr;
    for (const SubGroupYD& s : g->subGroups()) {
        if (s.name() == subName) sub = &s;
    }
    if (!sub) return;
    std::vector<std::string> candidates;
    std::vector<std::wstring> names;
    for (const std::string& id : g->members()) {
        if (UtilYD::contains(sub->members(), id)) continue;
        candidates.push_back(id);
        names.push_back(name(id));
    }
    if (candidates.empty()) {
        window_.info(L"讨论组拉人", L"群成员都已经在讨论组中。");
        return;
    }
    auto dialog = std::make_unique<DialogYD>(window_, L"拉人进入讨论组 " + GuiTextYD::wide(subName));
    dialog->pick(L"选择群成员（可多选）", names, true).primary(L"拉入", [this, candidates, subName](DialogYD& d) {
        std::vector<size_t> picked = d.picked();
        if (picked.empty()) {
            d.setError(L"请至少选择一位成员。");
            return false;
        }
        std::string all, result;
        GroupYD* grp = group();
        for (size_t i : picked) {
            if (grp) grp->addToSubGroup(me().id(), subName, candidates[i], result);
            all += (all.empty() ? "" : "；") + result;
        }
        report(all);
        return true;
    });
    window_.openDialog(std::move(dialog));
}
