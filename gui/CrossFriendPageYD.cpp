#include "CrossFriendPageYD.h"
#include "GuiTextYD.h"
#include "GuiWindowYD.h"
#include "MainViewYD.h"

CrossFriendPageYD::CrossFriendPageYD(GuiWindowYD& window) : PageYD(window) {}

void CrossFriendPageYD::build(PageModelYD& model) {
    ServiceBaseYD& m = me();
    const std::string current = m.serviceName();
    model.title = L"跨服务好友";
    model.subtitle = L"依据你在其他服务中的好友，在" + GuiTextYD::wide(current) + L"中添加好友、查询共同好友";
    std::vector<ServiceBaseYD*> others;
    for (ServiceBaseYD* s : user().services()) {
        if (s != &m) others.push_back(s);
    }
    if (others.empty()) {
        model.note(L"你只开通了一个服务，请先开通其他服务。");
        return;
    }
    bool valid = false;
    for (ServiceBaseYD* s : others) valid = valid || (hasSource_ && s->type() == source_);
    if (!valid) {   // 默认选第一个其他服务；当前服务切换后原来的选择可能失效
        source_ = others.front()->type();
        hasSource_ = true;
    }
    const std::string sourceName = ServiceTypeHelperYD::name(source_);

    model.section(L"选择另一个服务");
    std::vector<PageModelYD::ButtonYD> choices;
    for (ServiceBaseYD* s : others) {
        choices.push_back(PageModelYD::button(GuiTextYD::wide(s->serviceName()), ACT_SOURCE, ServiceTypeHelperYD::code(s->type()),
                                              s->type() == source_ ? PageModelYD::PRIMARY : PageModelYD::NORMAL));
    }
    model.actions(choices);

    // 推荐好友：source 服务中的好友，在当前服务中有能确认是同一人的账号、但还不是我的好友
    model.section(L"推荐添加：你的" + GuiTextYD::wide(sourceName) + L"好友，在" + GuiTextYD::wide(current) + L"中还不是好友");
    std::vector<ServiceBaseYD*> list;
    std::vector<std::string> skipped;
    std::string reason;
    if (!platform().recommendFriends(user(), m.type(), source_, list, skipped, reason)) {
        model.note(GuiTextYD::wide(reason));
        model.actions({ PageModelYD::button(L"去个人中心绑定QQ", ACT_ACCOUNT) });
    } else {
        if (list.empty()) model.note(L"暂无可推荐的好友（你的" + GuiTextYD::wide(sourceName) + L"好友没有开通" + GuiTextYD::wide(current) +
                                     L"、已经是你的好友，或无法确认是同一人）。");
        for (ServiceBaseYD* acc : list) {
            UserYD* owner = platform().ownerOf(acc);
            PageModelYD::RowYD& row = model.person(acc->id(), GuiTextYD::wide(acc->nickname()), GuiTextYD::wide(acc->displayName()),
                                                   L"← " + GuiTextYD::wide(sourceName) + L"好友 " +
                                                       GuiTextYD::wide(owner->service(source_)->displayName()));
            row.buttons.push_back(PageModelYD::button(L"加好友", ACT_ADD, acc->id(), PageModelYD::PRIMARY));
        }
        for (const std::string& s : skipped) model.note(L"（未推荐）" + GuiTextYD::wide(s));
    }

    // 共同好友：在两个服务中都是我好友、并且两边账号能确认是同一个人
    model.section(L"共同好友：在" + GuiTextYD::wide(current) + L"和" + GuiTextYD::wide(sourceName) + L"中都是你的好友");
    std::vector<UserYD*> common;
    if (!platform().commonFriends(user(), m.type(), source_, common, skipped, reason)) {
        model.note(GuiTextYD::wide(reason));
        return;
    }
    if (common.empty()) model.note(L"暂无");
    for (UserYD* u : common) {
        ServiceBaseYD* here = u->service(m.type());
        model.person(here->id(), GuiTextYD::wide(here->nickname()), GuiTextYD::wide(u->name()),
                     GuiTextYD::wide(current + " " + here->displayName() + "  /  " + sourceName + " " + u->service(source_)->displayName()));
    }
    for (const std::string& s : skipped) model.note(L"（未计入）" + GuiTextYD::wide(s));
}

void CrossFriendPageYD::onAction(int action, const std::string& arg) {
    switch (action) {
    case ACT_SOURCE:
        ServiceTypeHelperYD::parse(arg, source_);
        hasSource_ = true;
        break;
    case ACT_ADD: {
        std::string msg;
        platform().requestFriend(me(), arg, "", "", msg);   // 与微信相同：发送好友申请
        toast(GuiTextYD::wide(msg));
        refresh();
        break;
    }
    case ACT_ACCOUNT:
        main().showAccount();
        break;
    }
}
