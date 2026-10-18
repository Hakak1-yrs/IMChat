#include "GroupSquarePage.h"
#include "Conversation.h"
#include "Dialog.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "MainView.h"
#include "Session.h"
#include "Util.h"

GroupSquarePage::GroupSquarePage(GuiWindow& window) : Page(window) {
    window.session().loadAllGroups();   // 联网模式：向服务端取全部群
}

void GroupSquarePage::build(PageModel& model) {
    Session& s = session();
    const std::vector<GroupBrief> groups = s.allGroups();
    model.title = L"全部群（" + GuiText::wide(ServiceTypeHelper::name(s.me().type)) + L"）";
    model.subtitle = L"本服务共 " + std::to_wstring(groups.size()) + L" 个群，1001～1006 为平台预置群";
    model.headerButtons.push_back(PageModel::button(L"新建群", ACT_CREATE, "", PageModel::PRIMARY));
    for (const GroupBrief& g : groups) {
        const std::string no = std::to_string(g.groupNo);
        const std::string owner = g.owner.empty() ? "无（可申请认领）" : g.ownerName + "(" + g.owner + ")";
        PageModel::Row& row = model.person("G" + no, GuiText::wide(g.name), GuiText::wide(no + "  " + g.name),
                                           GuiText::wide(g.modeName + " · 群主 " + owner + " · " + std::to_string(g.memberCount) + " 人"),
                                           true);
        row.buttons.push_back(PageModel::button(L"详情", ACT_DETAIL, no));
        if (g.joined) {
            row.tags.push_back({ L"已加入", 0x2B7FFF });
            row.buttons.push_back(PageModel::button(L"进入群聊", ACT_CHAT, no, PageModel::PRIMARY));
        } else if (g.owner.empty()) {
            row.buttons.push_back(PageModel::button(L"认领", ACT_JOIN, no, PageModel::PRIMARY));
        } else if (g.applied) {
            row.tags.push_back({ L"已申请", 0xFF7A1A });
        } else if (g.allowApplyJoin) {
            row.buttons.push_back(PageModel::button(L"申请加入", ACT_JOIN, no, PageModel::PRIMARY));
        } else {
            row.tags.push_back({ L"只能推荐加入", 0x86909C });   // 微信群模式
        }
    }
}

void GroupSquarePage::onAction(int action, const std::string& arg) {
    switch (action) {
    case ACT_CREATE: createGroupDialog(window_); break;
    case ACT_DETAIL: main().showGroup(std::stoi(arg)); break;
    case ACT_CHAT: main().openChat(Conversation::groupChat(std::stoi(arg))); break;
    case ACT_JOIN: {
        GroupCommand command;
        command.kind = GroupCommand::JOIN;
        command.groupNo = std::stoi(arg);
        session().groupCommand(command, report());
        break;
    }
    }
}

void GroupSquarePage::createGroupDialog(GuiWindow& window) {
    auto dialog = std::make_unique<Dialog>(window, L"新建群");
    dialog->message(L"群号自动分配（从 1007 开始），你将成为群主，管理模式为本服务的默认模式。")
        .field(L"群名称")
        .primary(L"创建", [&window](Dialog& d) {
            if (Util::trim(d.text(0)).empty()) {
                d.setError(L"群名称不能为空。");
                return false;
            }
            window.session().createGroup(d.text(0), [&window](bool ok, const std::string& message, int groupNo) {
                window.mainView().reload();
                window.mainView().toast(GuiText::wide(message));
                if (ok) window.mainView().showGroup(groupNo);
            });
            return true;
        });
    window.openDialog(std::move(dialog));
}
