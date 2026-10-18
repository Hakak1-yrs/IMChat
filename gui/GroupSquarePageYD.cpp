#include "GroupSquarePageYD.h"
#include "ConversationYD.h"
#include "DialogYD.h"
#include "GuiTextYD.h"
#include "GuiWindowYD.h"
#include "MainViewYD.h"
#include "UtilYD.h"

GroupSquarePageYD::GroupSquarePageYD(GuiWindowYD& window) : PageYD(window) {}

void GroupSquarePageYD::build(PageModelYD& model) {
    ServiceBaseYD& m = me();
    std::vector<GroupYD*> groups = platform().groups().groupsOfService(m.type());
    model.title = L"全部群（" + GuiTextYD::wide(m.serviceName()) + L"）";
    model.subtitle = L"本服务共 " + std::to_wstring(groups.size()) + L" 个群，1001～1006 为平台预置群";
    model.headerButtons.push_back(PageModelYD::button(L"新建群", ACT_CREATE, "", PageModelYD::PRIMARY));
    for (GroupYD* g : groups) {
        const std::string no = std::to_string(g->groupNo());
        const std::string owner = g->hasOwner() ? platform().displayName(g->service(), g->ownerId()) : "无（可申请认领）";
        PageModelYD::RowYD& row = model.person("G" + no, GuiTextYD::wide(g->name()), GuiTextYD::wide(no + "  " + g->name()),
                                               GuiTextYD::wide(g->policy().modeName() + " · 群主 " + owner + " · " +
                                                               std::to_string(g->members().size()) + " 人"),
                                               true);
        row.buttons.push_back(PageModelYD::button(L"详情", ACT_DETAIL, no));
        if (g->isMember(m.id())) {
            row.tags.push_back({ L"已加入", 0x2B7FFF });
            row.buttons.push_back(PageModelYD::button(L"进入群聊", ACT_CHAT, no, PageModelYD::PRIMARY));
        } else if (!g->hasOwner()) {
            row.buttons.push_back(PageModelYD::button(L"认领", ACT_JOIN, no, PageModelYD::PRIMARY));
        } else if (UtilYD::contains(g->pending(), m.id())) {
            row.tags.push_back({ L"已申请", 0xFF7A1A });
        } else if (g->policy().allowApplyJoin()) {
            row.buttons.push_back(PageModelYD::button(L"申请加入", ACT_JOIN, no, PageModelYD::PRIMARY));
        } else {
            row.tags.push_back({ L"只能推荐加入", 0x86909C });   // 微信群模式
        }
    }
}

void GroupSquarePageYD::onAction(int action, const std::string& arg) {
    switch (action) {
    case ACT_CREATE: createGroupDialog(window_); break;
    case ACT_DETAIL: main().showGroup(std::stoi(arg)); break;
    case ACT_CHAT: main().openChat(ConversationYD::groupChat(std::stoi(arg))); break;
    case ACT_JOIN: {
        GroupYD* g = platform().groups().find(me().type(), std::stoi(arg));
        std::string msg;
        if (g) g->applyJoin(me().id(), msg);
        toast(GuiTextYD::wide(msg));
        refresh();
        break;
    }
    }
}

void GroupSquarePageYD::createGroupDialog(GuiWindowYD& window) {
    auto dialog = std::make_unique<DialogYD>(window, L"新建群");
    dialog->message(L"群号自动分配（从 1007 开始），你将成为群主，管理模式为本服务的默认模式。")
        .field(L"群名称")
        .primary(L"创建", [&window](DialogYD& d) {
            std::string msg;
            GroupYD* g = window.platform().createGroup(*window.session().current(), d.text(0), msg);
            if (!g) {
                d.setError(GuiTextYD::wide(msg));
                return false;
            }
            window.mainView().reload();
            window.mainView().toast(GuiTextYD::wide(msg));
            window.mainView().showGroup(g->groupNo());
            return true;
        });
    window.openDialog(std::move(dialog));
}
