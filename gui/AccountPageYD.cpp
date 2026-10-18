#include "AccountPageYD.h"
#include "DialogYD.h"
#include "GuiTextYD.h"
#include "GuiWindowYD.h"
#include "IndependentIdServiceYD.h"
#include "LoginViewYD.h"
#include "MainViewYD.h"
#include "PlatformToolsYD.h"
#include "ThemeYD.h"
#include "UtilYD.h"

AccountPageYD::AccountPageYD(GuiWindowYD& window) : PageYD(window) {}

std::string AccountPageYD::key() const { return "account"; }

void AccountPageYD::build(PageModelYD& model) {
    ServiceBaseYD& m = me();
    model.title = L"个人中心";
    model.subtitle = GuiTextYD::wide(m.serviceName() + " " + m.displayName() + " · 真实姓名 " + user().name());
    PageModelYD::RowYD& head =
        model.person(m.id(), GuiTextYD::wide(m.nickname()), GuiTextYD::wide(m.nickname()), GuiTextYD::wide(m.idLabel() + " " + m.id()));
    head.tags.push_back({ L"在线", 0x23C343 });

    // 资料：由虚函数 showInfo() 输出，QQ、微信、微博、微商各自追加特有的信息（多态）
    model.section(L"资料");
    for (const std::string& line : GuiTextYD::capture([&] { m.showInfo(); })) {
        const size_t colon = line.find("：");
        if (colon == std::string::npos) model.text(GuiTextYD::wide(line));
        else model.info(GuiTextYD::wide(line.substr(0, colon)), GuiTextYD::wide(line.substr(colon + std::string("：").size())));
    }
    auto* independent = dynamic_cast<IndependentIdServiceYD*>(&m);
    std::vector<PageModelYD::ButtonYD> edits = {
        PageModelYD::button(L"修改昵称", ACT_NICKNAME), PageModelYD::button(L"修改所在地", ACT_LOCATION),
        PageModelYD::button(L"修改出生日期", ACT_BIRTHDAY), PageModelYD::button(L"修改密码", ACT_PASSWORD) };
    if (independent) edits.push_back(PageModelYD::button(independent->isBound() ? L"解绑QQ" : L"绑定QQ", ACT_BIND));
    model.actions(edits);
    if (!independent && m.type() != ServiceTypeYD::QQ)
        model.note(GuiTextYD::wide(m.serviceName() + "与 QQ 共享号码，本来就对应你的 QQ 号，不需要绑定。"));

    // 好友列表、群列表（题目要求的用户基本信息）
    model.section(L"好友列表 · " + std::to_wstring(m.friends().size()));
    if (m.friends().size() == 0) model.note(L"还没有好友");
    for (const FriendYD& f : m.friends().all()) {
        ServiceBaseYD* acc = platform().findAccount(m.type(), f.id());
        PageModelYD::RowYD& row = model.person(f.id(), GuiTextYD::wide(acc ? acc->nickname() : f.id()),
                                               GuiTextYD::wide(platform().displayName(m.type(), f.id())),
                                               L"备注：" + GuiTextYD::wide(f.remark()));
        row.buttons.push_back(PageModelYD::button(L"资料", ACT_FRIEND, f.id()));
    }
    std::vector<GroupYD*> groups = platform().groups().groupsOf(m.type(), m.id());
    model.section(L"群列表 · " + std::to_wstring(groups.size()));
    if (groups.empty()) model.note(L"还没有加入任何群");
    for (GroupYD* g : groups) {
        PageModelYD::RowYD& row = model.person("G" + std::to_string(g->groupNo()), GuiTextYD::wide(g->name()),
                                               std::to_wstring(g->groupNo()) + L"  " + GuiTextYD::wide(g->name()),
                                               GuiTextYD::wide(g->policy().modeName()) + L" · " +
                                                   std::to_wstring(g->members().size()) + L" 人",
                                               true);
        row.buttons.push_back(PageModelYD::button(L"群设置", ACT_GROUP, std::to_string(g->groupNo())));
    }

    model.section(L"账号管理");
    model.actions({ PageModelYD::button(L"开通新服务", ACT_OPEN_SERVICE, "", PageModelYD::PRIMARY),
                    PageModelYD::button(L"关闭本服务（" + GuiTextYD::wide(m.serviceName()) + L"）", ACT_CLOSE_SERVICE, "",
                                        PageModelYD::DANGER),
                    PageModelYD::button(L"注销整个账号", ACT_DELETE_ACCOUNT, "", PageModelYD::DANGER) });
}

void AccountPageYD::onAction(int action, const std::string& arg) {
    switch (action) {
    case ACT_NICKNAME: editText(L"修改昵称", L"新昵称", me().nickname(), true); break;
    case ACT_LOCATION: editText(L"修改所在地", L"新所在地", me().location(), false); break;
    case ACT_BIRTHDAY: changeBirthday(); break;
    case ACT_PASSWORD: changePassword(); break;
    case ACT_BIND: bindQQ(); break;
    case ACT_OPEN_SERVICE: PlatformToolsYD::openServiceDialog(window_); break;
    case ACT_CLOSE_SERVICE: closeService(); break;
    case ACT_DELETE_ACCOUNT: deleteAccount(); break;
    case ACT_FRIEND: main().showFriend(arg); break;
    case ACT_GROUP: main().showGroup(std::stoi(arg)); break;
    }
}

void AccountPageYD::editText(const std::wstring& title, const std::wstring& label, const std::string& current,
                             bool nickname) {
    auto dialog = std::make_unique<DialogYD>(window_, title);
    dialog->field(label, GuiTextYD::wide(current)).primary(L"保存", [this, nickname](DialogYD& d) {
        const std::string value = UtilYD::toField(d.text(0));   // 与控制台相同：空格换成下划线，不能为空
        if (value.empty()) {
            d.setError(L"输入不能为空。");
            return false;
        }
        if (nickname) me().setNickname(value);
        else me().setLocation(value);
        main().updateTitle();
        refresh();
        toast(nickname ? L"昵称已修改" : L"所在地已修改");
        return true;
    });
    window_.openDialog(std::move(dialog));
}

void AccountPageYD::changeBirthday() {
    auto dialog = std::make_unique<DialogYD>(window_, L"修改出生日期");
    dialog->field(L"出生日期", GuiTextYD::wide(me().birthday().toString()), L"如 2004-05-20").primary(L"保存", [this](DialogYD& d) {
        DateYD birthday;
        if (!DateYD::parse(d.text(0), birthday) || birthday.toString() > DateYD::today().toString()) {
            d.setError(L"日期不正确（格式为 yyyy-mm-dd，且不能晚于今天）。");
            return false;
        }
        me().setBirthday(birthday);
        toast(L"出生日期已修改");
        return true;
    });
    window_.openDialog(std::move(dialog));
}

// 修改密码前先验证原密码；各服务账号的密码相互独立
void AccountPageYD::changePassword() {
    auto dialog = std::make_unique<DialogYD>(window_, L"修改密码（" + GuiTextYD::wide(me().serviceName()) + L"）");
    dialog->field(L"原密码", L"", L"", true).field(L"新密码", L"", L"至少 6 位", true).field(L"再次输入新密码", L"", L"", true);
    dialog->primary(L"修改", [this](DialogYD& d) {
        if (!me().checkPassword(UtilYD::toField(d.text(0)))) {
            d.setError(L"原密码错误。");
            return false;
        }
        const std::string next = UtilYD::toField(d.text(1));
        if (next.size() < 6) {
            d.setError(L"密码太短，请至少输入 6 位。");
            return false;
        }
        if (UtilYD::toField(d.text(2)) != next) {
            d.setError(L"两次输入的密码不一致。");
            return false;
        }
        me().setPassword(next);
        toast(L"密码已修改（只保存加密后的记录）");
        return true;
    });
    window_.openDialog(std::move(dialog));
}

void AccountPageYD::bindQQ() {
    auto* account = dynamic_cast<IndependentIdServiceYD*>(&me());
    if (!account) return;
    std::string msg;
    if (!account->isBound()) {
        platform().bindQQ(user(), *account, msg);
        window_.info(L"绑定QQ", GuiTextYD::wide(msg));
        refresh();
        return;
    }
    auto dialog = std::make_unique<DialogYD>(window_, L"解绑QQ");
    dialog->message(L"当前已绑定 QQ " + GuiTextYD::wide(account->boundQQ()) + L"。解绑后，" +
                    GuiTextYD::wide(account->serviceName()) + L"将不能再与 QQ、微博互认好友（共同好友、推荐好友）。")
        .primary(L"解绑", [this, account](DialogYD&) {
            std::string result;
            platform().unbindQQ(*account, result);
            toast(GuiTextYD::wide(result));
            refresh();
            return true;
        }, true);
    window_.openDialog(std::move(dialog));
}

void AccountPageYD::closeService() {
    if (user().services().size() == 1) {
        window_.info(L"关闭本服务", L"这是你唯一开通的服务，关闭后将无法登录。如需离开平台，请选择“注销整个账号”。");
        return;
    }
    auto dialog = std::make_unique<DialogYD>(window_, L"关闭" + GuiTextYD::wide(me().serviceName()));
    dialog->message(L"关闭后将删除该服务的账号、好友关系和群成员身份（群主身份会自动移交），无法恢复。")
        .field(L"请输入该服务的密码确认", L"", L"", true)
        .primary(L"确认关闭", [this](DialogYD& d) {
            if (!me().checkPassword(UtilYD::toField(d.text(0)))) {
                d.setError(L"密码错误。");
                return false;
            }
            main().endNetworkSessions();   // 群聊室、点对点聊天用到这个账号，先结束
            std::string msg;
            if (!session().closeCurrentService(msg)) {
                d.setError(GuiTextYD::wide(msg));
                return false;
            }
            main().afterServiceChanged();
            window_.info(L"已关闭服务", GuiTextYD::wide(msg + "\n当前服务已切换为 " + me().serviceName() + "。"));
            return true;
        }, true);
    window_.openDialog(std::move(dialog));
}

void AccountPageYD::deleteAccount() {
    auto dialog = std::make_unique<DialogYD>(window_, L"注销整个账号");
    dialog->message(L"注销将删除你在平台上的全部服务账号、好友关系和群成员身份，无法恢复。")
        .field(L"请输入当前服务的密码确认", L"", L"", true)
        .primary(L"确认注销", [this](DialogYD& d) {
            if (!me().checkPassword(UtilYD::toField(d.text(0)))) {
                d.setError(L"密码错误。");
                return false;
            }
            main().endNetworkSessions();
            std::string msg;
            session().deleteCurrentUser(msg);
            GuiWindowYD& window = window_;   // 回到登录页后本页面会被销毁，先把需要的对象取出来
            window.post([&window, msg] {
                window.showLogin();
                window.loginView().setNotice(GuiTextYD::wide(msg));
            });
            return true;
        }, true);
    window_.openDialog(std::move(dialog));
}
