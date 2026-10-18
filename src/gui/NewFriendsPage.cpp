#include "NewFriendsPage.h"
#include "Dialog.h"
#include "FriendPage.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "MainView.h"
#include "Util.h"

namespace {
const unsigned kGreen = 0x00A870;
const unsigned kGray = 0x86909C;
const unsigned kOrange = 0xFF7A1A;
}  // namespace

NewFriendsPage::NewFriendsPage(GuiWindow& window) : Page(window) {}

std::string NewFriendsPage::key() const { return "newfriends"; }

void NewFriendsPage::build(PageModel& model) {
    ServiceBase& m = me();
    model.title = L"新的朋友";
    model.subtitle = L"好友申请、好友验证与黑名单（" + GuiText::wide(m.serviceName()) + L"）";
    model.headerButtons.push_back(PageModel::button(L"添加好友", ACT_ADD, "", PageModel::PRIMARY));
    std::vector<FriendRequest> requests = platform().friendRequests(m);
    auto person = [&](const std::string& id, const std::wstring& subtitle) -> PageModel::Row& {
        ServiceBase* acc = platform().findAccount(m.type(), id);
        return model.person(id, GuiText::wide(acc ? acc->nickname() : id), GuiText::wide(platform().displayName(m.type(), id)), subtitle);
    };
    auto statusTag = [](const FriendRequest& r) -> PageModel::Tag {
        if (r.status == FriendRequest::ACCEPTED) return { L"已添加", kGreen };
        if (r.status == FriendRequest::REJECTED) return { L"已拒绝", kGray };
        return { L"等待验证", kOrange };
    };

    int incoming = 0;
    for (const FriendRequest& r : requests) incoming += r.to == m.id() ? 1 : 0;
    model.section(L"收到的申请 · " + std::to_wstring(incoming));
    if (incoming == 0) model.note(L"暂无好友申请");
    for (const FriendRequest& r : requests) {
        if (r.to != m.id()) continue;
        PageModel::Row& row = person(r.from, L"“" + GuiText::wide(r.greeting) + L"”  " + GuiText::listTime(r.time));
        if (r.status == FriendRequest::PENDING) {
            row.buttons.push_back(PageModel::button(L"同意", ACT_ACCEPT, std::to_string(r.seq), PageModel::PRIMARY));
            row.buttons.push_back(PageModel::button(L"拒绝", ACT_REJECT, std::to_string(r.seq)));
        } else {
            row.tags.push_back(statusTag(r));
            row.buttons.push_back(PageModel::button(L"资料", ACT_PROFILE, r.from));
        }
    }

    const size_t outgoing = requests.size() - incoming;
    if (outgoing > 0) {
        model.section(L"我发出的申请 · " + std::to_wstring(outgoing));
        for (const FriendRequest& r : requests) {
            if (r.from != m.id()) continue;
            person(r.to, L"“" + GuiText::wide(r.greeting) + L"”  " + GuiText::listTime(r.time)).tags.push_back(statusTag(r));
        }
    }

    model.section(L"隐私");
    const bool verify = platform().requiresVerification(m);
    model.info(L"好友验证", verify ? L"加我为好友时需要我同意" : L"不需要验证，别人添加后直接成为好友")
        .buttons.push_back(PageModel::button(verify ? L"关闭验证" : L"开启验证", ACT_VERIFY));

    std::vector<std::string> blocked = platform().blockedList(m);
    model.section(L"黑名单 · " + std::to_wstring(blocked.size()));
    model.note(L"拉黑后不再接收对方的消息和好友申请，对方也看不到你的" + GuiText::wide(m.momentsName()) + L"；好友关系保留。");
    for (const std::string& id : blocked) {
        person(id, GuiText::wide(id)).buttons.push_back(PageModel::button(L"移出黑名单", ACT_UNBLOCK, id));
    }
    model.actions({ PageModel::button(L"拉黑某个账号", ACT_BLOCK) });
}

void NewFriendsPage::onAction(int action, const std::string& arg) {
    std::string msg;
    switch (action) {
    case ACT_ADD: FriendPage::addFriendDialog(window_); return;
    case ACT_PROFILE: main().showFriend(arg); return;
    case ACT_REJECT:
        platform().respondRequest(me(), std::stoi(arg), false, "", msg);
        toast(GuiText::wide(msg));
        refresh();
        return;
    case ACT_ACCEPT: {   // 同意时可以顺便设置备注（与微信相同）
        std::string from;
        for (const FriendRequest& r : platform().friendRequests(me())) {
            if (r.seq == std::stoi(arg)) from = r.from;
        }
        ServiceBase* acc = platform().findAccount(me().type(), from);
        auto dialog = std::make_unique<Dialog>(window_, L"通过好友申请");
        dialog->message(L"通过 " + GuiText::wide(platform().displayName(me().type(), from)) + L" 的好友申请，你们将成为好友。")
            .field(L"备注", acc ? GuiText::wide(acc->nickname()) : L"")
            .primary(L"通过", [this, arg](Dialog& d) {
                std::string result;
                if (!platform().respondRequest(me(), std::stoi(arg), true, d.text(0), result)) {
                    d.setError(GuiText::wide(result));
                    return false;
                }
                toast(GuiText::wide(result));
                refresh();
                return true;
            });
        window_.openDialog(std::move(dialog));
        return;
    }
    case ACT_VERIFY:
        platform().setVerification(me(), !platform().requiresVerification(me()));
        toast(platform().requiresVerification(me()) ? L"已开启好友验证" : L"已关闭好友验证");
        return;
    case ACT_UNBLOCK:
        platform().setBlocked(me(), arg, false, msg);
        toast(GuiText::wide(msg));
        refresh();
        return;
    case ACT_BLOCK: {
        auto dialog = std::make_unique<Dialog>(window_, L"加入黑名单");
        dialog->field(L"对方账号").primary(L"拉黑", [this](Dialog& d) {
            std::string result;
            if (!platform().setBlocked(me(), Util::toField(d.text(0)), true, result)) {
                d.setError(GuiText::wide(result));
                return false;
            }
            toast(L"已加入黑名单");
            refresh();
            return true;
        }, true);
        window_.openDialog(std::move(dialog));
        return;
    }
    }
}
