#include "GroupPage.h"
#include <algorithm>
#include "Conversation.h"
#include "Dialog.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "MainView.h"
#include "Session.h"
#include "Util.h"

namespace {
const unsigned kOwnerColor = 0x2B7FFF;
const unsigned kAdminColor = 0xFF7A1A;
const unsigned kGrayColor = 0x86909C;

std::vector<std::wstring> splitLines(const std::string& text) {
    std::vector<std::wstring> lines;
    for (const std::string& line : Util::split(text, '\n')) lines.push_back(GuiText::wide(line));
    return lines;
}
}  // namespace

GroupPage::GroupPage(GuiWindow& window, int groupNo) : Page(window), groupNo_(groupNo) {
    window.session().loadGroup(groupNo_);   // 联网模式：去服务端取群详情；单机模式什么也不做
}

std::string GroupPage::key() const { return "G:" + std::to_string(groupNo_); }

std::wstring GroupPage::name(const std::string& id) { return GuiText::wide(session().nicknameOf(id) + "(" + id + ")"); }

GroupCommand GroupPage::command(GroupCommand::Kind kind, const std::string& target, bool on) const {
    GroupCommand c;
    c.kind = kind;
    c.groupNo = groupNo_;
    c.target = target;
    c.on = on;
    return c;
}

// ======================== 页面内容 ========================

void GroupPage::build(PageModel& model) {
    Session& s = session();
    const std::optional<GroupInfo> info = s.groupInfo(groupNo_);
    if (!info || !info->exists) {
        model.title = L"群 " + std::to_wstring(groupNo_);
        model.note(info ? L"该群已解散。" : L"正在获取群资料……");
        return;
    }
    const GroupInfo& g = *info;
    const bool member = g.member, owner = g.isOwner;
    model.title = GuiText::wide(g.name);
    model.subtitle = L"群号 " + std::to_wstring(groupNo_) + L" · " + GuiText::wide(g.modeName) + L" · " +
                     std::to_wstring(g.members.size()) + L" 人";
    if (member) {
        if (!s.isRemote()) model.headerButtons.push_back(PageModel::button(L"实时群聊室", ACT_ROOM));   // 基于本机 TCP 的旧功能
        model.headerButtons.push_back(PageModel::button(L"进入群聊", ACT_CHAT, "", PageModel::PRIMARY));
    }

    // ---- 群资料 ----
    model.section(L"群资料");
    PageModel::Row& nameRow = model.info(L"群名称", GuiText::wide(g.name));
    if (g.canEditInfo) nameRow.buttons.push_back(PageModel::button(L"修改", ACT_RENAME));
    model.info(L"群号", std::to_wstring(groupNo_) + (g.preset ? L"（平台预置群）" : L""));
    PageModel::Row& modeRow = model.info(L"管理模式", GuiText::wide(g.modeName));
    if (owner) modeRow.buttons.push_back(PageModel::button(g.mode == "QQ" ? L"切换为微信群模式" : L"切换为QQ群模式", ACT_SWITCH));
    model.info(L"群主", g.owner.empty() ? L"无（第一个申请加入的人将成为新群主）" : name(g.owner));
    const std::wstring role = owner ? L"群主" : g.isAdmin ? L"管理员" : member ? L"普通成员" : L"非成员";
    model.info(L"我的身份", role);
    PageModel::Row& noticeRow = model.info(L"群公告", g.notice.empty() ? L"无" : GuiText::wide(g.notice));
    if (g.canEditInfo) {
        noticeRow.buttons.push_back(PageModel::button(L"编辑", ACT_NOTICE));
        if (!g.notice.empty()) noticeRow.buttons.push_back(PageModel::button(L"清除", ACT_CLEAR_NOTICE));
    }

    // ---- 群特色：由当前管理策略的虚函数 showFeatures() 输出 ----
    model.section(L"群特色（随管理模式变化）");
    std::wstring features;
    for (const std::string& line : g.features) {
        if (line.find("【") == 0) continue;   // 标题行已在小标题中体现
        features += (features.empty() ? L"" : L"\n") + GuiText::wide(line);
    }
    model.text(features);

    // ---- 操作 ----
    std::vector<PageModel::Button> actions;
    if (!member) {
        if (g.applied) actions.push_back(PageModel::button(L"已申请，等待审批", 0));
        else actions.push_back(PageModel::button(g.owner.empty() ? L"认领本群" : L"申请加入", ACT_JOIN, "", PageModel::PRIMARY));
    } else {
        actions.push_back(PageModel::button(L"推荐好友入群", ACT_INVITE));
        if (g.allowSubGroup) actions.push_back(PageModel::button(L"新建讨论组", ACT_NEW_SUB));
        actions.push_back(PageModel::button(L"退出群", ACT_QUIT, "", PageModel::DANGER));
        if (g.canDissolve) actions.push_back(PageModel::button(L"解散群", ACT_DISSOLVE, "", PageModel::DANGER));
    }
    model.section(L"操作");
    model.actions(actions);
    if (!member && !g.allowApplyJoin && !g.owner.empty())
        model.note(L"当前为" + GuiText::wide(g.modeName) + L"，不能主动申请，只能由群成员推荐加入。");

    // ---- 成员：按钮按当前策略算出的权限出现 ----
    model.section(L"成员 · " + std::to_wstring(g.members.size()));
    for (const GroupMemberInfo& m : g.members) {
        PageModel::Row& row = model.person(m.id, GuiText::wide(m.nickname), name(m.id), GuiText::wide(m.id));
        if (m.owner) row.tags.push_back({ L"群主", kOwnerColor });
        else if (m.admin) row.tags.push_back({ L"管理员", kAdminColor });
        if (m.muted) row.tags.push_back({ L"禁言", kGrayColor });
        if (m.id == s.me().id) row.tags.push_back({ L"我", kGrayColor });
        if (m.canSetAdmin)
            row.buttons.push_back(m.admin ? PageModel::button(L"取消管理员", ACT_ADMIN_OFF, m.id)
                                          : PageModel::button(L"设为管理员", ACT_ADMIN_ON, m.id));
        if (m.canMute)
            row.buttons.push_back(m.muted ? PageModel::button(L"解除禁言", ACT_MUTE_OFF, m.id) : PageModel::button(L"禁言", ACT_MUTE_ON, m.id));
        if (m.canTransfer) row.buttons.push_back(PageModel::button(L"转让群主", ACT_TRANSFER, m.id));
        if (m.canKick) row.buttons.push_back(PageModel::button(L"踢出", ACT_KICK, m.id, PageModel::DANGER));
    }
    if (g.frozenAdmins > 0) model.note(L"原有 " + std::to_wstring(g.frozenAdmins) + L" 名管理员的数据已保留，当前模式下仅群主拥有特权。");
    if (g.frozenMuted > 0) model.note(L"原有 " + std::to_wstring(g.frozenMuted) + L" 条禁言记录已保留，当前模式下不生效。");

    // ---- 入群申请（QQ 群模式）----
    if (g.allowApplyJoin) {
        model.section(L"入群申请 · " + std::to_wstring(g.pending.size()));
        if (g.pending.empty()) model.note(L"暂无待审批的申请");
        for (const GroupMemberInfo& p : g.pending) {
            PageModel::Row& row = model.person(p.id, GuiText::wide(p.nickname), name(p.id), L"申请加入本群");
            if (g.canApprove) {
                row.buttons.push_back(PageModel::button(L"同意", ACT_APPROVE, p.id, PageModel::PRIMARY));
                row.buttons.push_back(PageModel::button(L"拒绝", ACT_REJECT, p.id));
            }
        }
        if (!g.pending.empty() && !g.canApprove) model.note(L"只有群主或管理员可以审批入群申请。");
    }

    // ---- 临时讨论组（QQ 群模式）----
    model.section(L"临时讨论组 · " + std::to_wstring(g.subGroups.size()));
    if (!g.allowSubGroup) {
        model.note(g.subGroups.empty() ? L"当前模式不支持临时讨论组。"
                                       : L"原有 " + std::to_wstring(g.subGroups.size()) + L" 个讨论组的数据已保留，切换回QQ群模式后可用。");
    } else if (g.subGroups.empty()) {
        model.note(L"暂无讨论组");
    }
    for (const SubGroupInfo& sub : g.subGroups) {
        std::wstring members;
        for (const std::string& id : sub.members) members += (members.empty() ? L"" : L"、") + name(id);
        PageModel::Row& row = model.person("S" + sub.name, GuiText::wide(sub.name), GuiText::wide(sub.name),
                                           std::to_wstring(sub.members.size()) + L" 人：" + members, true);
        if (sub.canAdd) row.buttons.push_back(PageModel::button(L"拉人", ACT_SUB_ADD, sub.name));
        if (sub.canDissolve) row.buttons.push_back(PageModel::button(L"解散", ACT_SUB_DISSOLVE, sub.name, PageModel::DANGER));
    }
}

// ======================== 操作 ========================

void GroupPage::confirm(const std::wstring& title, const std::wstring& text, const std::wstring& button, bool danger,
                          const GroupCommand& command) {
    auto dialog = std::make_unique<Dialog>(window_, title);
    dialog->message(text).primary(button, [this, command](Dialog&) {
        session().groupCommand(command, report());
        return true;
    }, danger);
    window_.openDialog(std::move(dialog));
}

void GroupPage::onAction(int action, const std::string& arg) {
    const std::optional<GroupInfo> info = session().groupInfo(groupNo_);
    if (!info || !info->exists) return;
    const GroupInfo& g = *info;
    switch (action) {
    case ACT_CHAT: main().openChat(Conversation::groupChat(groupNo_)); return;
    case ACT_ROOM: main().showRoom(groupNo_); return;
    case ACT_JOIN: session().groupCommand(command(GroupCommand::JOIN), report()); return;
    case ACT_RENAME: {
        auto dialog = std::make_unique<Dialog>(window_, L"修改群名称");
        dialog->field(L"新群名称", GuiText::wide(g.name)).primary(L"保存", [this](Dialog& d) {
            if (Util::trim(d.text(0)).empty()) {
                d.setError(L"群名称不能为空。");
                return false;
            }
            GroupCommand c = command(GroupCommand::RENAME);
            c.text = d.text(0);
            session().groupCommand(c, report());
            return true;
        });
        window_.openDialog(std::move(dialog));
        return;
    }
    case ACT_NOTICE: editNotice(g); return;
    case ACT_CLEAR_NOTICE: confirm(L"清除群公告", L"确认清除群公告？", L"清除", true, command(GroupCommand::SET_NOTICE)); return;
    case ACT_SWITCH: switchMode(g); return;
    case ACT_INVITE: inviteFriend(); return;
    case ACT_QUIT:
        confirm(L"退出群", L"确认退出群 " + GuiText::wide(g.name) + L"？", L"退出", true, command(GroupCommand::QUIT));
        return;
    case ACT_DISSOLVE: {
        if (main().roomActive(groupNo_)) {
            window_.info(L"解散群", L"本群的实时群聊室正在进行，请先关闭群聊室。");
            return;
        }
        auto dialog = std::make_unique<Dialog>(window_, L"解散群");
        dialog->message(L"解散后成员、讨论组和群聊记录都将删除，无法恢复。确认解散群 " + std::to_wstring(groupNo_) + L"（" +
                        GuiText::wide(g.name) + L"）？")
            .primary(L"解散", [this](Dialog&) {
                GuiWindow& window = window_;   // 结果可能晚到（联网模式），回调里只引用窗口
                session().groupCommand(command(GroupCommand::DISSOLVE), [&window](bool ok, const std::string& message) {
                    window.mainView().toast(GuiText::wide(message));
                    window.mainView().reload();
                    if (ok) window.mainView().showSquare();
                });
                return true;
            }, true);
        window_.openDialog(std::move(dialog));
        return;
    }
    case ACT_NEW_SUB: createSubGroup(g); return;
    case ACT_SUB_ADD: addToSubGroup(g, arg); return;
    case ACT_SUB_DISSOLVE:
        confirm(L"解散讨论组", L"确认解散讨论组 " + GuiText::wide(arg) + L"？", L"解散", true, command(GroupCommand::DISSOLVE_SUB_GROUP, arg));
        return;
    case ACT_ADMIN_ON:
    case ACT_ADMIN_OFF: session().groupCommand(command(GroupCommand::SET_ADMIN, arg, action == ACT_ADMIN_ON), report()); return;
    case ACT_MUTE_ON:
    case ACT_MUTE_OFF: session().groupCommand(command(GroupCommand::SET_MUTED, arg, action == ACT_MUTE_ON), report()); return;
    case ACT_KICK: confirm(L"踢出成员", L"确认把 " + name(arg) + L" 移出本群？", L"踢出", true, command(GroupCommand::KICK, arg)); return;
    case ACT_TRANSFER:
        confirm(L"转让群主", L"确认把群主转让给 " + name(arg) + L"？转让后你将成为普通成员。", L"转让", false,
                command(GroupCommand::TRANSFER, arg));
        return;
    case ACT_APPROVE:
    case ACT_REJECT: session().groupCommand(command(GroupCommand::APPROVE, arg, action == ACT_APPROVE), report()); return;
    }
}

void GroupPage::editNotice(const GroupInfo& g) {
    auto dialog = std::make_unique<Dialog>(window_, L"发布群公告");
    dialog->message(L"发布后会同时作为一条群消息发出，群成员会收到未读提醒。")
        .field(L"公告内容", GuiText::wide(g.notice), L"最多 150 个字")
        .primary(L"发布", [this](Dialog& d) {
            const std::string text = Util::trim(d.text(0));
            if (text.empty()) {
                d.setError(L"公告内容不能为空（清除公告请用“清除”按钮）。");
                return false;
            }
            if (Util::charCount(text) > 150) {
                d.setError(L"群公告太长（最多 150 个字）。");
                return false;
            }
            GroupCommand c = command(GroupCommand::SET_NOTICE);
            c.text = text;
            session().groupCommand(c, report());
            return true;
        });
    window_.openDialog(std::move(dialog));
}

void GroupPage::switchMode(const GroupInfo& g) {
    const bool toWeChat = g.mode == "QQ";
    const std::wstring targetName = toWeChat ? L"微信群模式" : L"QQ群模式";
    auto dialog = std::make_unique<Dialog>(window_, L"切换群管理模式");
    dialog->message(L"把群 " + std::to_wstring(groupNo_) + L" 从" + GuiText::wide(g.modeName) + L"切换为" + targetName +
                    L"？只更换管理规则，群主、成员、管理员、禁言记录、讨论组等数据保持不变。")
        .primary(L"切换", [this, toWeChat](Dialog&) {
            GroupCommand c = command(GroupCommand::SWITCH_MODE);
            c.text = toWeChat ? "WECHAT" : "QQ";
            GuiWindow& window = window_;
            // 结果有多行：切换结果、切换前后的数据比对、新模式的群特色
            session().groupCommand(c, [&window](bool ok, const std::string& message) {
                window.mainView().reload();
                if (!ok) {
                    window.info(L"切换群管理模式", GuiText::wide(message));
                    return;
                }
                auto done = std::make_unique<Dialog>(window, L"切换完成");
                done->lines(splitLines(message)).cancelText(L"知道了");
                window.openDialog(std::move(done));
            });
            return true;
        });
    window_.openDialog(std::move(dialog));
}

void GroupPage::inviteFriend() {
    const std::optional<GroupInfo> g = session().groupInfo(groupNo_);
    std::vector<std::string> candidates;
    std::vector<std::wstring> names;
    for (const Conversation& f : session().contacts()) {
        const bool inGroup = std::any_of(g->members.begin(), g->members.end(), [&](const GroupMemberInfo& m) { return m.id == f.peerId; });
        if (inGroup) continue;
        candidates.push_back(f.peerId);
        names.push_back(name(f.peerId));
    }
    if (candidates.empty()) {
        window_.info(L"推荐好友入群", L"没有可推荐的好友（好友都已在群中，或你还没有好友）。");
        return;
    }
    auto dialog = std::make_unique<Dialog>(window_, L"推荐好友入群");
    dialog->pick(L"选择好友", names, false).primary(L"推荐", [this, candidates](Dialog& d) {
        std::vector<size_t> picked = d.picked();
        if (picked.empty()) {
            d.setError(L"请选择一位好友。");
            return false;
        }
        session().groupCommand(command(GroupCommand::INVITE, candidates[picked[0]]), report());
        return true;
    });
    window_.openDialog(std::move(dialog));
}

void GroupPage::createSubGroup(const GroupInfo& g) {
    std::vector<std::string> candidates;
    std::vector<std::wstring> names;
    for (const GroupMemberInfo& m : g.members) {
        if (m.id == session().me().id) continue;
        candidates.push_back(m.id);
        names.push_back(name(m.id));
    }
    auto dialog = std::make_unique<Dialog>(window_, L"新建临时讨论组");
    dialog->field(L"讨论组名称").pick(L"从群成员中拉人（可多选）", names, true).primary(L"创建", [this, candidates](Dialog& d) {
        if (Util::trim(d.text(0)).empty()) {
            d.setError(L"讨论组名称不能为空。");
            return false;
        }
        GroupCommand c = command(GroupCommand::NEW_SUB_GROUP);
        c.text = d.text(0);
        for (size_t i : d.picked()) c.members.push_back(candidates[i]);
        session().groupCommand(c, report());
        return true;
    });
    window_.openDialog(std::move(dialog));
}

void GroupPage::addToSubGroup(const GroupInfo& g, const std::string& subName) {
    auto sub = std::find_if(g.subGroups.begin(), g.subGroups.end(), [&](const SubGroupInfo& s) { return s.name == subName; });
    if (sub == g.subGroups.end()) return;
    std::vector<std::string> candidates;
    std::vector<std::wstring> names;
    for (const GroupMemberInfo& m : g.members) {
        if (Util::contains(sub->members, m.id)) continue;
        candidates.push_back(m.id);
        names.push_back(name(m.id));
    }
    if (candidates.empty()) {
        window_.info(L"讨论组拉人", L"群成员都已经在讨论组中。");
        return;
    }
    auto dialog = std::make_unique<Dialog>(window_, L"拉人进入讨论组 " + GuiText::wide(subName));
    dialog->pick(L"选择群成员（可多选）", names, true).primary(L"拉入", [this, candidates, subName](Dialog& d) {
        std::vector<size_t> picked = d.picked();
        if (picked.empty()) {
            d.setError(L"请至少选择一位成员。");
            return false;
        }
        GroupCommand c = command(GroupCommand::ADD_TO_SUB_GROUP, subName);
        for (size_t i : picked) c.members.push_back(candidates[i]);
        session().groupCommand(c, report());
        return true;
    });
    window_.openDialog(std::move(dialog));
}
