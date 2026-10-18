#include "PlatformTools.h"
#include "Dialog.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "IndependentIdService.h"
#include "LoginView.h"
#include "MainView.h"
#include "Util.h"

void PlatformTools::showOverview(GuiWindow& window) {
    Platform& platform = window.platform();
    std::vector<std::wstring> lines;
    for (const auto& kv : platform.users()) {
        const User& u = kv.second;
        std::string line = "用户" + std::to_string(u.id()) + "  " + u.name() + "：";
        for (ServiceBase* s : u.services()) line += "  " + s->serviceName() + "[" + s->id() + "]";
        lines.push_back(GuiText::wide(line));
    }
    std::string groups = "群数量：";
    for (ServiceType t : ServiceTypeHelper::all())
        groups += ServiceTypeHelper::name(t) + " " + std::to_string(platform.groups().groupsOfService(t).size()) + " 个  ";
    lines.push_back(GuiText::wide(groups));
    lines.push_back(L"（预置演示账号的密码均为 123456）");
    auto dialog = std::make_unique<Dialog>(window, L"平台概况");
    dialog->message(L"共 " + std::to_wstring(platform.users().size()) + L" 位用户").lines(lines).cancelText(L"关闭");
    window.openDialog(std::move(dialog));
}

void PlatformTools::saveNow(GuiWindow& window) {
    bool ok = false;
    // 保存时如果发现数据文件被其他窗口改过，业务类会输出提示（整组另存到冲突目录），这里收集起来显示
    std::vector<std::string> output = GuiText::capture([&] { ok = window.platform().save(); });
    std::string text;
    for (const std::string& line : output) text += line + "\n";
    if (!ok || text.find("[注意]") != std::string::npos) {
        window.info(ok ? L"数据已另存" : L"保存失败", GuiText::wide(text.empty() ? "保存失败，请检查 data 目录。" : text));
    } else if (window.localSession().loggedIn()) {
        window.mainView().toast(L"数据已保存");
    } else {
        window.info(L"立即保存数据", L"数据已保存。");
    }
}

void PlatformTools::addProfileFields(Dialog& dialog) {
    dialog.field(L"昵称", L"", L"请输入昵称")
        .field(L"出生日期", L"", L"如 2004-05-20")
        .field(L"所在地", L"", L"如 北京")
        .field(L"密码", L"", L"至少 6 位", true)
        .field(L"再次输入密码", L"", L"", true);
}

bool PlatformTools::readProfile(Dialog& dialog, size_t first, std::string& nickname, Date& birthday,
                                  std::string& location, std::string& password) {
    nickname = Util::toField(dialog.text(first));
    location = Util::toField(dialog.text(first + 2));
    password = Util::toField(dialog.text(first + 3));
    if (nickname.empty() || location.empty()) {
        dialog.setError(L"昵称和所在地不能为空。");
        return false;
    }
    // yyyy-mm-dd 格式的字符串可以直接按字典序比较先后
    if (!Date::parse(dialog.text(first + 1), birthday) || birthday.toString() > Date::today().toString()) {
        dialog.setError(L"出生日期不正确（格式为 yyyy-mm-dd，且不能晚于今天）。");
        return false;
    }
    if (password.size() < 6) {
        dialog.setError(L"密码太短，请至少输入 6 位。");
        return false;
    }
    if (Util::toField(dialog.text(first + 4)) != password) {
        dialog.setError(L"两次输入的密码不一致。");
        return false;
    }
    return true;
}

void PlatformTools::registerDialog(GuiWindow& window) {
    std::vector<ServiceType> all = ServiceTypeHelper::all();
    std::vector<std::wstring> names;
    for (ServiceType t : all) names.push_back(GuiText::wide(ServiceTypeHelper::name(t)));
    auto dialog = std::make_unique<Dialog>(window, L"注册新用户");
    dialog->field(L"真实姓名", L"", L"请输入真实姓名").choice(L"首个开通的服务", names, 0);
    addProfileFields(*dialog);
    dialog->primary(L"注册", [&window, all](Dialog& d) {
        Platform& platform = window.platform();
        const std::string name = Util::toField(d.text(0));
        const ServiceType type = all[d.selected(0)];
        std::string msg;
        if (name.empty()) {
            d.setError(L"真实姓名不能为空。");
            return false;
        }
        if (!platform.canRegisterWith(type, msg)) {   // 例如微博与 QQ 共享号码，不能作为第一个服务
            d.setError(GuiText::wide(msg));
            return false;
        }
        std::string nickname, location, password;
        Date birthday;
        if (!readProfile(d, 1, nickname, birthday, location, password)) return false;
        ServiceBase* account = platform.registerUser(name, type, nickname, birthday, location, password, msg);
        if (!account) {
            d.setError(GuiText::wide(msg));
            return false;
        }
        window.loginView().prefill(type, account->id());
        window.info(L"注册成功", GuiText::wide(msg) + L"\n已为你填好账号，输入密码即可登录。");
        return true;
    });
    window.openDialog(std::move(dialog));
}

void PlatformTools::openServiceDialog(GuiWindow& window) {
    User& user = *window.localSession().user();
    std::vector<ServiceType> options;
    std::vector<std::wstring> names;
    for (ServiceType t : ServiceTypeHelper::all()) {
        if (user.hasService(t)) continue;
        options.push_back(t);
        names.push_back(GuiText::wide(ServiceTypeHelper::name(t)));
    }
    if (options.empty()) {
        window.info(L"开通新服务", L"你已开通平台上的全部服务。");
        return;
    }
    auto dialog = std::make_unique<Dialog>(window, L"开通新服务");
    dialog->choice(L"服务", names, 0);
    addProfileFields(*dialog);
    dialog->choice(L"与本人的 QQ 号绑定（仅微信、微商有效）", { L"绑定", L"不绑定" }, 0);
    dialog->primary(L"开通", [&window, options](Dialog& d) {
        LocalSession& session = window.localSession();
        User& owner = *session.user();
        std::string nickname, location, password, msg;
        Date birthday;
        if (!readProfile(d, 0, nickname, birthday, location, password)) return false;
        ServiceBase* account =
            window.platform().openService(owner, options[d.selected(0)], nickname, birthday, location, password, msg);
        if (!account) {
            d.setError(GuiText::wide(msg));
            return false;
        }
        account->login();   // 已有服务在线，新开通的服务直接进入登录状态
        // 微信、微商等独立号码的服务：绑定 QQ 号后才能与 QQ、微博互认好友
        auto* independent = dynamic_cast<IndependentIdService*>(account);
        if (independent && owner.hasService(ServiceType::QQ) && d.selected(1) == 0) {
            std::string bindMsg;
            window.platform().bindQQ(owner, *independent, bindMsg);
            msg += "\n" + bindMsg;
        }
        window.mainView().reload();
        window.info(L"开通成功", GuiText::wide(msg));
        return true;
    });
    window.openDialog(std::move(dialog));
}
