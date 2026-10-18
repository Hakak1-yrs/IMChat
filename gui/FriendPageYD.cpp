#include "FriendPageYD.h"
#include "ConversationYD.h"
#include "DialogYD.h"
#include "GuiTextYD.h"
#include "GuiWindowYD.h"
#include "MainViewYD.h"
#include "MomentsPageYD.h"
#include "UtilYD.h"

FriendPageYD::FriendPageYD(GuiWindowYD& window, const std::string& peerId) : PageYD(window), peerId_(peerId) {}

std::string FriendPageYD::key() const { return "P:" + peerId_; }

void FriendPageYD::addFriendDialog(GuiWindowYD& window) {
    ServiceBaseYD& m = *window.session().current();
    auto dialog = std::make_unique<DialogYD>(window, L"添加" + GuiTextYD::wide(m.serviceName()) + L"好友");
    dialog->message(L"对方开启了好友验证时，需要对方同意后才能成为好友。")
        .field(L"对方账号", L"", GuiTextYD::wide(m.serviceName() + "中的账号"))
        .field(L"验证消息", L"我是" + GuiTextYD::wide(m.nickname()))
        .field(L"备注", L"", L"不填则使用对方昵称")
        .primary(L"发送申请", [&window](DialogYD& d) {
            const std::string id = UtilYD::toField(d.text(0));
            if (id.empty()) {
                d.setError(L"请输入对方账号。");
                return false;
            }
            std::string msg;
            ServiceBaseYD& me = *window.session().current();
            if (!window.platform().requestFriend(me, id, d.text(1), d.text(2), msg)) {
                d.setError(GuiTextYD::wide(msg));
                return false;
            }
            window.mainView().reload();
            window.mainView().toast(GuiTextYD::wide(msg));
            if (me.friends().contains(id)) window.mainView().showFriend(id);   // 对方不需要验证：已经是好友
            return true;
        });
    window.openDialog(std::move(dialog));
}

void FriendPageYD::build(PageModelYD& model) {
    ServiceBaseYD& m = me();
    ServiceBaseYD* other = platform().findAccount(m.type(), peerId_);
    if (!other) {
        model.title = GuiTextYD::wide(peerId_);
        model.note(L"该账号已关闭或注销。");
        return;
    }
    const FriendYD* f = m.friends().find(peerId_);
    model.title = GuiTextYD::wide(f ? f->remark() : other->nickname());
    model.subtitle = GuiTextYD::wide(m.serviceName() + " " + peerId_);
    PageModelYD::RowYD& head = model.person(peerId_, GuiTextYD::wide(other->nickname()), GuiTextYD::wide(other->displayName()),
                                            GuiTextYD::wide(other->idLabel() + " " + other->id()));
    if (f) head.tags.push_back({ L"好友", 0x2B7FFF });
    else head.tags.push_back({ L"非好友", 0x86909C });
    const bool blocked = platform().isBlocked(m.type(), m.id(), peerId_);
    if (blocked) head.tags.push_back({ L"已拉黑", 0xF54A45 });   // 之后还会添加行，head 引用随后可能失效，必须在这里用完

    // 多态：按好友账号的实际类型显示资料
    model.section(L"资料");
    for (const std::string& line : GuiTextYD::capture([&] { other->showInfo(); })) {
        const size_t colon = line.find("：");
        if (colon == std::string::npos) model.text(GuiTextYD::wide(line));
        else model.info(GuiTextYD::wide(line.substr(0, colon)), GuiTextYD::wide(line.substr(colon + std::string("：").size())));
    }
    if (f) model.info(L"我给他的备注", GuiTextYD::wide(f->remark())).buttons.push_back(PageModelYD::button(L"修改备注", ACT_REMARK));

    std::vector<std::string> mutual = platform().mutualFriends(m, *other);
    model.section(L"我们的共同好友 · " + std::to_wstring(mutual.size()));
    if (mutual.empty()) model.note(L"暂无");
    for (const std::string& id : mutual) {
        ServiceBaseYD* acc = platform().findAccount(m.type(), id);
        model.person(id, GuiTextYD::wide(acc ? acc->nickname() : id), GuiTextYD::wide(platform().displayName(m.type(), id)), L"");
    }

    const std::wstring blockText = blocked ? L"移出黑名单" : L"加入黑名单";
    if (f) {
        if (!other->friends().contains(m.id())) model.note(L"对方已把你从好友列表中删除（单向删除）：你发的消息会被拒收，需要重新发送好友申请。");
        model.actions({ PageModelYD::button(L"发消息", ACT_CHAT, "", PageModelYD::PRIMARY),
                        PageModelYD::button(L"TA的" + GuiTextYD::wide(m.momentsName()), ACT_MOMENTS),
                        PageModelYD::button(blockText, ACT_BLOCK), PageModelYD::button(L"删除好友", ACT_DELETE, "", PageModelYD::DANGER) });
    } else {
        model.note(L"对方不是你的好友。");
        model.actions({ PageModelYD::button(L"添加好友", ACT_ADD, "", PageModelYD::PRIMARY), PageModelYD::button(blockText, ACT_BLOCK) });
    }
}

void FriendPageYD::onAction(int action, const std::string&) {
    std::string msg;
    switch (action) {
    case ACT_CHAT:
        main().openChat(ConversationYD::privateChat(peerId_));
        break;
    case ACT_ADD:
        platform().requestFriend(me(), peerId_, "", "", msg);
        toast(GuiTextYD::wide(msg));
        refresh();
        break;
    case ACT_MOMENTS:
        main().showPage(std::make_shared<MomentsPageYD>(window_, peerId_));
        break;
    case ACT_BLOCK:
        platform().setBlocked(me(), peerId_, !platform().isBlocked(me().type(), me().id(), peerId_), msg);
        toast(GuiTextYD::wide(msg));
        refresh();
        break;
    case ACT_REMARK: {
        const FriendYD* f = me().friends().find(peerId_);
        auto dialog = std::make_unique<DialogYD>(window_, L"修改好友备注");
        dialog->field(L"新备注", f ? GuiTextYD::wide(f->remark()) : L"").primary(L"保存", [this](DialogYD& d) {
            const std::string remark = UtilYD::toField(d.text(0));
            if (remark.empty()) {
                d.setError(L"备注不能为空。");
                return false;
            }
            me().friends().modifyRemark(peerId_, remark);
            toast(L"备注已修改");
            refresh();
            return true;
        });
        window_.openDialog(std::move(dialog));
        break;
    }
    case ACT_DELETE: {
        auto dialog = std::make_unique<DialogYD>(window_, L"删除好友");
        dialog->message(L"确认删除 " + GuiTextYD::wide(platform().displayName(me().type(), peerId_)) +
                        L"？与微信相同，删除是单向的：只从你的列表中删除，对方列表里仍有你，之后对方发来的消息会被拒收。")
            .primary(L"删除", [this](DialogYD&) {
                std::string result;
                platform().removeFriend(me(), peerId_, result);
                toast(GuiTextYD::wide(result));
                refresh();
                return true;
            }, true);
        window_.openDialog(std::move(dialog));
        break;
    }
    }
}
