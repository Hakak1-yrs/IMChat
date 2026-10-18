#include "CrossFriendPage.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "MainView.h"

CrossFriendPage::CrossFriendPage(GuiWindow& window) : Page(window) {}

void CrossFriendPage::build(PageModel& model) {
    ServiceBase& m = me();
    const std::string current = m.serviceName();
    model.title = L"跨服务好友";
    model.subtitle = L"依据你在其他服务中的好友，在" + GuiText::wide(current) + L"中添加好友、查询共同好友";
    std::vector<ServiceBase*> others;
    for (ServiceBase* s : user().services()) {
        if (s != &m) others.push_back(s);
    }
    if (others.empty()) {
        model.note(L"你只开通了一个服务，请先开通其他服务。");
        return;
    }
    bool valid = false;
    for (ServiceBase* s : others) valid = valid || (hasSource_ && s->type() == source_);
    if (!valid) {   // 默认选第一个其他服务；当前服务切换后原来的选择可能失效
        source_ = others.front()->type();
        hasSource_ = true;
    }
    const std::string sourceName = ServiceTypeHelper::name(source_);

    model.section(L"选择另一个服务");
    std::vector<PageModel::Button> choices;
    for (ServiceBase* s : others) {
        choices.push_back(PageModel::button(GuiText::wide(s->serviceName()), ACT_SOURCE, ServiceTypeHelper::code(s->type()),
                                              s->type() == source_ ? PageModel::PRIMARY : PageModel::NORMAL));
    }
    model.actions(choices);

    // 推荐好友：source 服务中的好友，在当前服务中有能确认是同一人的账号、但还不是我的好友
    model.section(L"推荐添加：你的" + GuiText::wide(sourceName) + L"好友，在" + GuiText::wide(current) + L"中还不是好友");
    std::vector<ServiceBase*> list;
    std::vector<std::string> skipped;
    std::string reason;
    if (!platform().recommendFriends(user(), m.type(), source_, list, skipped, reason)) {
        model.note(GuiText::wide(reason));
        model.actions({ PageModel::button(L"去个人中心绑定QQ", ACT_ACCOUNT) });
    } else {
        if (list.empty()) model.note(L"暂无可推荐的好友（你的" + GuiText::wide(sourceName) + L"好友没有开通" + GuiText::wide(current) +
                                     L"、已经是你的好友，或无法确认是同一人）。");
        for (ServiceBase* acc : list) {
            User* owner = platform().ownerOf(acc);
            PageModel::Row& row = model.person(acc->id(), GuiText::wide(acc->nickname()), GuiText::wide(acc->displayName()),
                                                   L"← " + GuiText::wide(sourceName) + L"好友 " +
                                                       GuiText::wide(owner->service(source_)->displayName()));
            row.buttons.push_back(PageModel::button(L"加好友", ACT_ADD, acc->id(), PageModel::PRIMARY));
        }
        for (const std::string& s : skipped) model.note(L"（未推荐）" + GuiText::wide(s));
    }

    // 共同好友：在两个服务中都是我好友、并且两边账号能确认是同一个人
    model.section(L"共同好友：在" + GuiText::wide(current) + L"和" + GuiText::wide(sourceName) + L"中都是你的好友");
    std::vector<User*> common;
    if (!platform().commonFriends(user(), m.type(), source_, common, skipped, reason)) {
        model.note(GuiText::wide(reason));
        return;
    }
    if (common.empty()) model.note(L"暂无");
    for (User* u : common) {
        ServiceBase* here = u->service(m.type());
        model.person(here->id(), GuiText::wide(here->nickname()), GuiText::wide(u->name()),
                     GuiText::wide(current + " " + here->displayName() + "  /  " + sourceName + " " + u->service(source_)->displayName()));
    }
    for (const std::string& s : skipped) model.note(L"（未计入）" + GuiText::wide(s));
}

void CrossFriendPage::onAction(int action, const std::string& arg) {
    switch (action) {
    case ACT_SOURCE:
        ServiceTypeHelper::parse(arg, source_);
        hasSource_ = true;
        break;
    case ACT_ADD: {
        std::string msg;
        platform().requestFriend(me(), arg, "", "", msg);   // 与微信相同：发送好友申请
        toast(GuiText::wide(msg));
        refresh();
        break;
    }
    case ACT_ACCOUNT:
        main().showAccount();
        break;
    }
}
