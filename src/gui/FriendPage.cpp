#include "FriendPage.h"
#include "Conversation.h"
#include "Dialog.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "MainView.h"
#include "MomentsPage.h"
#include "Util.h"

FriendPage::FriendPage(GuiWindow& window, const std::string& peerId) : Page(window), peerId_(peerId) {}

std::string FriendPage::key() const { return "P:" + peerId_; }

void FriendPage::addFriendDialog(GuiWindow& window) {
    ServiceBase& m = *window.session().current();
    auto dialog = std::make_unique<Dialog>(window, L"添加" + GuiText::wide(m.serviceName()) + L"好友");
    dialog->message(L"对方开启了好友验证时，需要对方同意后才能成为好友。")
        .field(L"对方账号", L"", GuiText::wide(m.serviceName() + "中的账号"))
        .field(L"验证消息", L"我是" + GuiText::wide(m.nickname()))
        .field(L"备注", L"", L"不填则使用对方昵称")
        .primary(L"发送申请", [&window](Dialog& d) {
            const std::string id = Util::toField(d.text(0));
            if (id.empty()) {
                d.setError(L"请输入对方账号。");
                return false;
            }
            std::string msg;
            ServiceBase& me = *window.session().current();
            if (!window.platform().requestFriend(me, id, d.text(1), d.text(2), msg)) {
                d.setError(GuiText::wide(msg));
                return false;
            }
            window.mainView().reload();
            window.mainView().toast(GuiText::wide(msg));
            if (me.friends().contains(id)) window.mainView().showFriend(id);   // 对方不需要验证：已经是好友
            return true;
        });
    window.openDialog(std::move(dialog));
}

void FriendPage::build(PageModel& model) {
    ServiceBase& m = me();
    ServiceBase* other = platform().findAccount(m.type(), peerId_);
    if (!other) {
        model.title = GuiText::wide(peerId_);
        model.note(L"该账号已关闭或注销。");
        return;
    }
    const Friend* f = m.friends().find(peerId_);
    model.title = GuiText::wide(f ? f->remark() : other->nickname());
    model.subtitle = GuiText::wide(m.serviceName() + " " + peerId_);
    PageModel::Row& head = model.person(peerId_, GuiText::wide(other->nickname()), GuiText::wide(other->displayName()),
                                            GuiText::wide(other->idLabel() + " " + other->id()));
    if (f) head.tags.push_back({ L"好友", 0x2B7FFF });
    else head.tags.push_back({ L"非好友", 0x86909C });
    const bool blocked = platform().isBlocked(m.type(), m.id(), peerId_);
    if (blocked) head.tags.push_back({ L"已拉黑", 0xF54A45 });   // 之后还会添加行，head 引用随后可能失效，必须在这里用完

    // 多态：按好友账号的实际类型显示资料
    model.section(L"资料");
    for (const std::string& line : GuiText::capture([&] { other->showInfo(); })) {
        const size_t colon = line.find("：");
        if (colon == std::string::npos) model.text(GuiText::wide(line));
        else model.info(GuiText::wide(line.substr(0, colon)), GuiText::wide(line.substr(colon + std::string("：").size())));
    }
    if (f) model.info(L"我给他的备注", GuiText::wide(f->remark())).buttons.push_back(PageModel::button(L"修改备注", ACT_REMARK));

    std::vector<std::string> mutual = platform().mutualFriends(m, *other);
    model.section(L"我们的共同好友 · " + std::to_wstring(mutual.size()));
    if (mutual.empty()) model.note(L"暂无");
    for (const std::string& id : mutual) {
        ServiceBase* acc = platform().findAccount(m.type(), id);
        model.person(id, GuiText::wide(acc ? acc->nickname() : id), GuiText::wide(platform().displayName(m.type(), id)), L"");
    }

    const std::wstring blockText = blocked ? L"移出黑名单" : L"加入黑名单";
    if (f) {
        if (!other->friends().contains(m.id())) model.note(L"对方已把你从好友列表中删除（单向删除）：你发的消息会被拒收，需要重新发送好友申请。");
        model.actions({ PageModel::button(L"发消息", ACT_CHAT, "", PageModel::PRIMARY),
                        PageModel::button(L"TA的" + GuiText::wide(m.momentsName()), ACT_MOMENTS),
                        PageModel::button(blockText, ACT_BLOCK), PageModel::button(L"删除好友", ACT_DELETE, "", PageModel::DANGER) });
    } else {
        model.note(L"对方不是你的好友。");
        model.actions({ PageModel::button(L"添加好友", ACT_ADD, "", PageModel::PRIMARY), PageModel::button(blockText, ACT_BLOCK) });
    }
}

void FriendPage::onAction(int action, const std::string&) {
    std::string msg;
    switch (action) {
    case ACT_CHAT:
        main().openChat(Conversation::privateChat(peerId_));
        break;
    case ACT_ADD:
        platform().requestFriend(me(), peerId_, "", "", msg);
        toast(GuiText::wide(msg));
        refresh();
        break;
    case ACT_MOMENTS:
        main().showPage(std::make_shared<MomentsPage>(window_, peerId_));
        break;
    case ACT_BLOCK:
        platform().setBlocked(me(), peerId_, !platform().isBlocked(me().type(), me().id(), peerId_), msg);
        toast(GuiText::wide(msg));
        refresh();
        break;
    case ACT_REMARK: {
        const Friend* f = me().friends().find(peerId_);
        auto dialog = std::make_unique<Dialog>(window_, L"修改好友备注");
        dialog->field(L"新备注", f ? GuiText::wide(f->remark()) : L"").primary(L"保存", [this](Dialog& d) {
            const std::string remark = Util::toField(d.text(0));
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
        auto dialog = std::make_unique<Dialog>(window_, L"删除好友");
        dialog->message(L"确认删除 " + GuiText::wide(platform().displayName(me().type(), peerId_)) +
                        L"？与微信相同，删除是单向的：只从你的列表中删除，对方列表里仍有你，之后对方发来的消息会被拒收。")
            .primary(L"删除", [this](Dialog&) {
                std::string result;
                platform().removeFriend(me(), peerId_, result);
                toast(GuiText::wide(result));
                refresh();
                return true;
            }, true);
        window_.openDialog(std::move(dialog));
        break;
    }
    }
}
