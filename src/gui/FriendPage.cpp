#include "FriendPage.h"
#include "Conversation.h"
#include "Dialog.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "MainView.h"
#include "MomentsPage.h"
#include "Util.h"

FriendPage::FriendPage(GuiWindow& window, const std::string& peerId) : Page(window), peerId_(peerId) {
    window.session().loadProfile(peerId_);   // 联网模式：去服务端取完整资料；单机模式什么也不做
}

std::string FriendPage::key() const { return "P:" + peerId_; }

void FriendPage::addFriendDialog(GuiWindow& window) {
    const AccountView me = window.session().me();
    const std::string serviceName = ServiceTypeHelper::name(me.type);
    auto dialog = std::make_unique<Dialog>(window, L"添加" + GuiText::wide(serviceName) + L"好友");
    dialog->message(L"对方开启了好友验证时，需要对方同意后才能成为好友。")
        .field(L"对方账号", L"", GuiText::wide(serviceName + "中的账号"))
        .field(L"验证消息", L"我是" + GuiText::wide(me.nickname))
        .field(L"备注", L"", L"不填则使用对方昵称")
        .primary(L"发送申请", [&window](Dialog& d) {
            const std::string id = Util::toField(d.text(0));
            if (id.empty()) {
                d.setError(L"请输入对方账号。");
                return false;
            }
            // 结果稍后提示（联网模式要等服务端响应）；对方不需要验证时已经直接成为好友，打开他的资料页
            window.session().requestFriend(id, d.text(1), d.text(2), [&window, id](bool ok, const std::string& message) {
                window.mainView().toast(GuiText::wide(message));
                window.mainView().reload();
                if (ok && window.session().profile(id).isFriend) window.mainView().showFriend(id);
            });
            return true;
        });
    window.openDialog(std::move(dialog));
}

void FriendPage::build(PageModel& model) {
    Session& s = session();
    const AccountView me = s.me();
    const ProfileView other = s.profile(peerId_);
    if (!other.known) {
        model.title = GuiText::wide(peerId_);
        model.note(s.isRemote() ? L"正在获取资料……" : L"该账号已关闭或注销。");
        return;
    }
    model.title = GuiText::wide(other.isFriend && !other.remark.empty() ? other.remark : other.nickname);
    model.subtitle = GuiText::wide(ServiceTypeHelper::name(me.type) + " " + peerId_);
    PageModel::Row& head = model.person(peerId_, GuiText::wide(other.nickname), GuiText::wide(other.nickname + "(" + peerId_ + ")"),
                                        GuiText::wide((other.idLabel.empty() ? std::string("账号") : other.idLabel) + " " + peerId_));
    head.tags.push_back(other.isFriend ? PageModel::Tag{ L"好友", 0x2B7FFF } : PageModel::Tag{ L"非好友", 0x86909C });
    if (other.showOnline) head.tags.push_back(other.online ? PageModel::Tag{ L"在线", 0x00A870 } : PageModel::Tag{ L"离线", 0x86909C });
    if (other.blocked) head.tags.push_back({ L"已拉黑", 0xF54A45 });   // 之后还会添加行，head 引用随后可能失效，必须在这里用完

    // 多态：按对方账号的实际类型显示资料
    model.section(L"资料");
    if (other.info.empty()) {   // 联网模式下完整资料还没取回来
        model.info(L"昵称", GuiText::wide(other.nickname));
        model.info(L"所在地", GuiText::wide(other.location));
    }
    for (const std::string& line : other.info) {
        const size_t colon = line.find("：");
        if (colon == std::string::npos) model.text(GuiText::wide(line));
        else model.info(GuiText::wide(line.substr(0, colon)), GuiText::wide(line.substr(colon + std::string("：").size())));
    }
    if (other.isFriend)
        model.info(L"我给他的备注", GuiText::wide(other.remark.empty() ? "（未设置）" : other.remark))
            .buttons.push_back(PageModel::button(L"修改备注", ACT_REMARK));

    model.section(L"我们的共同好友 · " + std::to_wstring(other.mutualFriends.size()));
    if (other.mutualFriends.empty()) model.note(L"暂无");
    for (const std::string& id : other.mutualFriends) {
        const std::string nickname = s.nicknameOf(id);
        model.person(id, GuiText::wide(nickname), GuiText::wide(nickname + "(" + id + ")"), L"");
    }

    const std::wstring blockText = other.blocked ? L"移出黑名单" : L"加入黑名单";
    if (other.isFriend) {
        if (!other.peerHasMe) model.note(L"对方已把你从好友列表中删除（单向删除）：你发的消息会被拒收，需要重新发送好友申请。");
        std::vector<PageModel::Button> buttons{ PageModel::button(L"发消息", ACT_CHAT, "", PageModel::PRIMARY) };
        if (!s.isRemote()) buttons.push_back(PageModel::button(L"TA的" + GuiText::wide(me.momentsName), ACT_MOMENTS));   // 朋友圈暂未联网
        buttons.push_back(PageModel::button(blockText, ACT_BLOCK));
        buttons.push_back(PageModel::button(L"删除好友", ACT_DELETE, "", PageModel::DANGER));
        model.actions(buttons);
    } else {
        model.note(L"对方不是你的好友。");
        model.actions({ PageModel::button(L"添加好友", ACT_ADD, "", PageModel::PRIMARY), PageModel::button(blockText, ACT_BLOCK) });
    }
}

void FriendPage::onAction(int action, const std::string&) {
    switch (action) {
    case ACT_CHAT:
        main().openChat(Conversation::privateChat(peerId_));
        break;
    case ACT_ADD:
        session().requestFriend(peerId_, "", "", report());
        break;
    case ACT_MOMENTS:
        main().showPage(std::make_shared<MomentsPage>(window_, peerId_));
        break;
    case ACT_BLOCK:
        session().setBlocked(peerId_, !session().profile(peerId_).blocked, report());
        break;
    case ACT_REMARK: {
        auto dialog = std::make_unique<Dialog>(window_, L"修改好友备注");
        dialog->field(L"新备注", GuiText::wide(session().profile(peerId_).remark)).primary(L"保存", [this](Dialog& d) {
            if (Util::trim(d.text(0)).empty()) {
                d.setError(L"备注不能为空。");
                return false;
            }
            session().setRemark(peerId_, d.text(0), report());
            return true;
        });
        window_.openDialog(std::move(dialog));
        break;
    }
    case ACT_DELETE: {
        const ProfileView other = session().profile(peerId_);
        auto dialog = std::make_unique<Dialog>(window_, L"删除好友");
        dialog->message(L"确认删除 " + GuiText::wide(other.nickname + "(" + peerId_ + ")") +
                        L"？与微信相同，删除是单向的：只从你的列表中删除，对方列表里仍有你，之后对方发来的消息会被拒收。")
            .primary(L"删除", [this](Dialog&) {
                session().removeFriend(peerId_, report());
                return true;
            }, true);
        window_.openDialog(std::move(dialog));
        break;
    }
    }
}
