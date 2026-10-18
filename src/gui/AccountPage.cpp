#include "AccountPage.h"
#include "Dialog.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "IndependentIdService.h"
#include "LoginView.h"
#include "MainView.h"
#include "PlatformTools.h"
#include "Theme.h"
#include "Util.h"

AccountPage::AccountPage(GuiWindow& window) : Page(window) {}

std::string AccountPage::key() const { return "account"; }

void AccountPage::build(PageModel& model) {
    ServiceBase& m = me();
    model.title = L"个人中心";
    model.subtitle = GuiText::wide(m.serviceName() + " " + m.displayName() + " · 真实姓名 " + user().name());
    PageModel::Row& head =
        model.person(m.id(), GuiText::wide(m.nickname()), GuiText::wide(m.nickname()), GuiText::wide(m.idLabel() + " " + m.id()));
    head.tags.push_back({ L"在线", 0x23C343 });

    // 资料：由虚函数 showInfo() 输出，QQ、微信、微博、微商各自追加特有的信息（多态）
    model.section(L"资料");
    for (const std::string& line : GuiText::capture([&] { m.showInfo(); })) {
        const size_t colon = line.find("：");
        if (colon == std::string::npos) model.text(GuiText::wide(line));
        else model.info(GuiText::wide(line.substr(0, colon)), GuiText::wide(line.substr(colon + std::string("：").size())));
    }
    auto* independent = dynamic_cast<IndependentIdService*>(&m);
    std::vector<PageModel::Button> edits = {
        PageModel::button(L"修改昵称", ACT_NICKNAME), PageModel::button(L"修改所在地", ACT_LOCATION),
        PageModel::button(L"修改出生日期", ACT_BIRTHDAY), PageModel::button(L"修改密码", ACT_PASSWORD) };
    if (independent) edits.push_back(PageModel::button(independent->isBound() ? L"解绑QQ" : L"绑定QQ", ACT_BIND));
    model.actions(edits);
    if (!independent && m.type() != ServiceType::QQ)
        model.note(GuiText::wide(m.serviceName() + "与 QQ 共享号码，本来就对应你的 QQ 号，不需要绑定。"));

    // 好友列表、群列表（题目要求的用户基本信息）
    model.section(L"好友列表 · " + std::to_wstring(m.friends().size()));
    if (m.friends().size() == 0) model.note(L"还没有好友");
    for (const Friend& f : m.friends().all()) {
        ServiceBase* acc = platform().findAccount(m.type(), f.id());
        PageModel::Row& row = model.person(f.id(), GuiText::wide(acc ? acc->nickname() : f.id()),
                                               GuiText::wide(platform().displayName(m.type(), f.id())),
                                               L"备注：" + GuiText::wide(f.remark()));
        row.buttons.push_back(PageModel::button(L"资料", ACT_FRIEND, f.id()));
    }
    std::vector<Group*> groups = platform().groups().groupsOf(m.type(), m.id());
    model.section(L"群列表 · " + std::to_wstring(groups.size()));
    if (groups.empty()) model.note(L"还没有加入任何群");
    for (Group* g : groups) {
        PageModel::Row& row = model.person("G" + std::to_string(g->groupNo()), GuiText::wide(g->name()),
                                               std::to_wstring(g->groupNo()) + L"  " + GuiText::wide(g->name()),
                                               GuiText::wide(g->policy().modeName()) + L" · " +
                                                   std::to_wstring(g->members().size()) + L" 人",
                                               true);
        row.buttons.push_back(PageModel::button(L"群设置", ACT_GROUP, std::to_string(g->groupNo())));
    }

    model.section(L"账号管理");
    model.actions({ PageModel::button(L"开通新服务", ACT_OPEN_SERVICE, "", PageModel::PRIMARY),
                    PageModel::button(L"关闭本服务（" + GuiText::wide(m.serviceName()) + L"）", ACT_CLOSE_SERVICE, "",
                                        PageModel::DANGER),
                    PageModel::button(L"注销整个账号", ACT_DELETE_ACCOUNT, "", PageModel::DANGER) });
}

void AccountPage::onAction(int action, const std::string& arg) {
    switch (action) {
    case ACT_NICKNAME: editText(L"修改昵称", L"新昵称", me().nickname(), true); break;
    case ACT_LOCATION: editText(L"修改所在地", L"新所在地", me().location(), false); break;
    case ACT_BIRTHDAY: changeBirthday(); break;
    case ACT_PASSWORD: changePassword(); break;
    case ACT_BIND: bindQQ(); break;
    case ACT_OPEN_SERVICE: PlatformTools::openServiceDialog(window_); break;
    case ACT_CLOSE_SERVICE: closeService(); break;
    case ACT_DELETE_ACCOUNT: deleteAccount(); break;
    case ACT_FRIEND: main().showFriend(arg); break;
    case ACT_GROUP: main().showGroup(std::stoi(arg)); break;
    }
}

void AccountPage::editText(const std::wstring& title, const std::wstring& label, const std::string& current,
                             bool nickname) {
    auto dialog = std::make_unique<Dialog>(window_, title);
    dialog->field(label, GuiText::wide(current)).primary(L"保存", [this, nickname](Dialog& d) {
        const std::string value = Util::toField(d.text(0));   // 与控制台相同：空格换成下划线，不能为空
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

void AccountPage::changeBirthday() {
    auto dialog = std::make_unique<Dialog>(window_, L"修改出生日期");
    dialog->field(L"出生日期", GuiText::wide(me().birthday().toString()), L"如 2004-05-20").primary(L"保存", [this](Dialog& d) {
        Date birthday;
        if (!Date::parse(d.text(0), birthday) || birthday.toString() > Date::today().toString()) {
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
void AccountPage::changePassword() {
    auto dialog = std::make_unique<Dialog>(window_, L"修改密码（" + GuiText::wide(me().serviceName()) + L"）");
    dialog->field(L"原密码", L"", L"", true).field(L"新密码", L"", L"至少 6 位", true).field(L"再次输入新密码", L"", L"", true);
    dialog->primary(L"修改", [this](Dialog& d) {
        if (!me().checkPassword(Util::toField(d.text(0)))) {
            d.setError(L"原密码错误。");
            return false;
        }
        const std::string next = Util::toField(d.text(1));
        if (next.size() < 6) {
            d.setError(L"密码太短，请至少输入 6 位。");
            return false;
        }
        if (Util::toField(d.text(2)) != next) {
            d.setError(L"两次输入的密码不一致。");
            return false;
        }
        me().setPassword(next);
        toast(L"密码已修改（只保存加密后的记录）");
        return true;
    });
    window_.openDialog(std::move(dialog));
}

void AccountPage::bindQQ() {
    auto* account = dynamic_cast<IndependentIdService*>(&me());
    if (!account) return;
    std::string msg;
    if (!account->isBound()) {
        platform().bindQQ(user(), *account, msg);
        window_.info(L"绑定QQ", GuiText::wide(msg));
        refresh();
        return;
    }
    auto dialog = std::make_unique<Dialog>(window_, L"解绑QQ");
    dialog->message(L"当前已绑定 QQ " + GuiText::wide(account->boundQQ()) + L"。解绑后，" +
                    GuiText::wide(account->serviceName()) + L"将不能再与 QQ、微博互认好友（共同好友、推荐好友）。")
        .primary(L"解绑", [this, account](Dialog&) {
            std::string result;
            platform().unbindQQ(*account, result);
            toast(GuiText::wide(result));
            refresh();
            return true;
        }, true);
    window_.openDialog(std::move(dialog));
}

void AccountPage::closeService() {
    if (user().services().size() == 1) {
        window_.info(L"关闭本服务", L"这是你唯一开通的服务，关闭后将无法登录。如需离开平台，请选择“注销整个账号”。");
        return;
    }
    auto dialog = std::make_unique<Dialog>(window_, L"关闭" + GuiText::wide(me().serviceName()));
    dialog->message(L"关闭后将删除该服务的账号、好友关系和群成员身份（群主身份会自动移交），无法恢复。")
        .field(L"请输入该服务的密码确认", L"", L"", true)
        .primary(L"确认关闭", [this](Dialog& d) {
            if (!me().checkPassword(Util::toField(d.text(0)))) {
                d.setError(L"密码错误。");
                return false;
            }
            main().endNetworkSessions();   // 群聊室、点对点聊天用到这个账号，先结束
            std::string msg;
            if (!local().closeCurrentService(msg)) {
                d.setError(GuiText::wide(msg));
                return false;
            }
            main().afterServiceChanged();
            window_.info(L"已关闭服务", GuiText::wide(msg + "\n当前服务已切换为 " + me().serviceName() + "。"));
            return true;
        }, true);
    window_.openDialog(std::move(dialog));
}

void AccountPage::deleteAccount() {
    auto dialog = std::make_unique<Dialog>(window_, L"注销整个账号");
    dialog->message(L"注销将删除你在平台上的全部服务账号、好友关系和群成员身份，无法恢复。")
        .field(L"请输入当前服务的密码确认", L"", L"", true)
        .primary(L"确认注销", [this](Dialog& d) {
            if (!me().checkPassword(Util::toField(d.text(0)))) {
                d.setError(L"密码错误。");
                return false;
            }
            main().endNetworkSessions();
            std::string msg;
            local().deleteCurrentUser(msg);
            GuiWindow& window = window_;   // 回到登录页后本页面会被销毁，先把需要的对象取出来
            window.post([&window, msg] {
                window.showLogin();
                window.loginView().setNotice(GuiText::wide(msg));
            });
            return true;
        }, true);
    window_.openDialog(std::move(dialog));
}
