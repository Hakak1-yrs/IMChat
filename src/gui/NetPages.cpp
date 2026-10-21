#include "SocketOps.h"   // 内含 winsock2.h，必须先于其他 Windows 头文件
#include "NetPages.h"
#include <chrono>
#include "Dialog.h"
#include "Frame.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "LoginView.h"
#include "MainView.h"
#include "Packet.h"
#include "Password.h"
#include "RemoteAccounts.h"
#include "Util.h"

using json = nlohmann::json;

namespace {

// 资料里的一行“键：值”拆成两列
void infoLines(PageModel& model, const json& lines) {
    for (const json& item : lines) {
        const std::string line = item.is_string() ? item.get<std::string>() : "";
        const size_t colon = line.find("：");
        if (colon == std::string::npos) model.text(GuiText::wide(line));
        else model.info(GuiText::wide(line.substr(0, colon)), GuiText::wide(line.substr(colon + std::string("：").size())));
    }
}

std::wstring serviceName(const std::string& code) {
    ServiceType type = ServiceType::QQ;
    return ServiceTypeHelper::parse(code, type) ? GuiText::wide(ServiceTypeHelper::name(type)) : GuiText::wide(code);
}

// 与单机版相同的资料输入框和检查（服务端会再检查一遍）
void addProfileFields(Dialog& dialog) {
    dialog.field(L"昵称", L"", L"请输入昵称")
        .field(L"出生日期", L"", L"如 2004-05-20")
        .field(L"所在地", L"", L"如 北京")
        .field(L"密码", L"", L"至少 6 位", true)
        .field(L"再次输入密码", L"", L"", true);
}

bool readProfile(Dialog& d, size_t first, json& args) {
    args["nickname"] = d.text(first);
    args["birthday"] = d.text(first + 1);
    args["location"] = d.text(first + 2);
    args["password"] = d.text(first + 3);
    if (d.text(first).empty() || d.text(first + 2).empty()) {
        d.setError(L"昵称和所在地不能为空。");
        return false;
    }
    if (d.text(first + 3).size() < 6) {
        d.setError(L"密码太短，请至少输入 6 位。");
        return false;
    }
    if (d.text(first + 4) != d.text(first + 3)) {
        d.setError(L"两次输入的密码不一致。");
        return false;
    }
    return true;
}

unsigned long long now() { return GetTickCount64(); }

}  // namespace

// ======================== 个人中心 ========================

NetAccountPage::NetAccountPage(GuiWindow& window) : Page(window) { load(); }

std::string NetAccountPage::key() const { return "account"; }

void NetAccountPage::load() {
    RemoteSession* remote = window_.remoteSession();
    if (!remote) return;
    std::weak_ptr<int> alive = alive_;
    GuiWindow& window = window_;
    remote->call("account.get", json::object(), [this, alive, &window](bool ok, const json& data, const std::string& error) {
        if (alive.expired()) return;
        if (ok) data_ = data;
        else error_ = error;
        window.invalidate();
    });
}

void NetAccountPage::build(PageModel& model) {
    model.title = L"个人中心";
    if (data_.is_null()) {
        model.note(error_.empty() ? L"正在获取……" : GuiText::wide(error_));
        return;
    }
    const std::string id = data_.value("id", "");
    model.subtitle = GuiText::wide(data_.value("serviceName", "") + " " + data_.value("nickname", "") + "(" + id + ") · 真实姓名 " +
                                   data_.value("name", ""));
    PageModel::Row& head = model.person(id, GuiText::wide(data_.value("nickname", "")), GuiText::wide(data_.value("nickname", "")),
                                        GuiText::wide(data_.value("idLabel", "") + " " + id));
    head.tags.push_back({ L"在线", 0x23C343 });

    // 资料：服务端的账号对象用虚函数 showInfo() 输出，QQ、微信、微博、微商各自追加特有的信息（多态）
    model.section(L"资料");
    infoLines(model, data_.value("info", json::array()));
    std::vector<PageModel::Button> edits = { PageModel::button(L"修改昵称", ACT_NICKNAME), PageModel::button(L"修改所在地", ACT_LOCATION),
                                             PageModel::button(L"修改出生日期", ACT_BIRTHDAY), PageModel::button(L"修改密码", ACT_PASSWORD) };
    const bool independent = data_.value("independent", false);
    const std::string bound = data_.value("boundQQ", "");
    if (independent) edits.push_back(PageModel::button(bound.empty() ? L"绑定QQ" : L"解绑QQ", ACT_BIND));
    model.actions(edits);
    if (!independent && data_.value("service", "") != "QQ")
        model.note(GuiText::wide(data_.value("serviceName", "") + "与 QQ 共享号码，本来就对应你的 QQ 号，不需要绑定。"));
    else if (independent && !data_.value("hasQQ", false))
        model.note(L"你还没有开通 QQ。开通 QQ 后可以绑定，与 QQ、微博互认好友。");

    const json services = data_.value("services", json::array());
    model.section(L"已开通的服务 · " + std::to_wstring(services.size()));
    for (const json& s : services) {
        const std::string code = s.value("service", "");
        PageModel::Row& row = model.person(s.value("account", ""), GuiText::wide(s.value("nickname", "")),
                                           serviceName(code) + L"  " + GuiText::wide(s.value("nickname", "")),
                                           GuiText::wide(s.value("account", "")));
        if (code == data_.value("service", "")) row.tags.push_back({ L"当前", 0x23C343 });
        else row.buttons.push_back(PageModel::button(L"切换", ACT_SWITCH, code));
    }

    model.section(L"账号管理");
    std::vector<PageModel::Button> manage;
    if (!data_.value("available", json::array()).empty()) manage.push_back(PageModel::button(L"开通新服务", ACT_OPEN_SERVICE, "", PageModel::PRIMARY));
    manage.push_back(PageModel::button(L"关闭本服务（" + GuiText::wide(data_.value("serviceName", "")) + L"）", ACT_CLOSE_SERVICE, "", PageModel::DANGER));
    manage.push_back(PageModel::button(L"注销整个账号", ACT_DEREGISTER, "", PageModel::DANGER));
    model.actions(manage);
}

void NetAccountPage::update(const char* field, const std::wstring& title, const std::wstring& label, const std::string& current) {
    const std::string name = field;
    auto dialog = std::make_unique<Dialog>(window_, title);
    dialog->field(label, GuiText::wide(current)).primary(L"保存", [this, name](Dialog& d) {
        if (d.text(0).empty()) {
            d.setError(L"输入不能为空。");
            return false;
        }
        RemoteSession* remote = window_.remoteSession();
        std::weak_ptr<int> alive = alive_;
        GuiWindow& window = window_;
        const std::string value = d.text(0);
        remote->call("account.update", json{ { name, value } }, [this, alive, &window, name, value, remote](bool ok, const json& data, const std::string& error) {
            window.mainView().toast(GuiText::wide(ok ? data.value("message", "") : error));
            if (!ok) return;
            if (name == "nickname") remote->setProfile(Util::toField(value), "");
            if (name == "location") remote->setProfile("", Util::toField(value));
            window.mainView().updateTitle();
            window.mainView().reload();
            if (!alive.expired()) load();
        });
        return true;
    });
    window_.openDialog(std::move(dialog));
}

void NetAccountPage::onAction(int action, const std::string& arg) {
    RemoteSession* remote = window_.remoteSession();
    if (!remote || data_.is_null()) return;
    GuiWindow& window = window_;
    std::weak_ptr<int> alive = alive_;
    switch (action) {
    case ACT_NICKNAME: update("nickname", L"修改昵称", L"新昵称", data_.value("nickname", "")); break;
    case ACT_LOCATION: update("location", L"修改所在地", L"新所在地", data_.value("location", "")); break;
    case ACT_BIRTHDAY: update("birthday", L"修改出生日期", L"出生日期（如 2004-05-20）", data_.value("birthday", "")); break;
    case ACT_PASSWORD: {
        auto dialog = std::make_unique<Dialog>(window_, L"修改密码（" + GuiText::wide(data_.value("serviceName", "")) + L"）");
        dialog->field(L"原密码", L"", L"", true).field(L"新密码", L"", L"至少 6 位", true).field(L"再次输入新密码", L"", L"", true);
        dialog->primary(L"修改", [remote, &window](Dialog& d) {
            if (d.text(1) != d.text(2)) {
                d.setError(L"两次输入的密码不一致。");
                return false;
            }
            remote->call("account.password", json{ { "old", d.text(0) }, { "new", d.text(1) } }, [&window](bool ok, const json& data, const std::string& error) {
                window.mainView().toast(GuiText::wide(ok ? data.value("message", "") : error));
            });
            return true;
        });
        window_.openDialog(std::move(dialog));
        break;
    }
    case ACT_BIND: {
        const bool bind = data_.value("boundQQ", "").empty();
        remote->call("account.bindQQ", json{ { "on", bind } }, [this, alive, &window](bool ok, const json& data, const std::string& error) {
            window.info(L"绑定QQ", GuiText::wide(ok ? data.value("message", "") : error));
            if (!alive.expired()) load();
        });
        break;
    }
    case ACT_SWITCH: {
        ServiceType type = ServiceType::QQ;
        if (ServiceTypeHelper::parse(arg, type)) main().switchService(type);
        break;
    }
    case ACT_OPEN_SERVICE:
        NetTools::openServiceDialog(window_);
        break;
    case ACT_CLOSE_SERVICE: {
        if (data_.value("services", json::array()).size() == 1) {
            window_.info(L"关闭本服务", L"这是你唯一开通的服务，关闭后将无法登录。如需离开平台，请选择“注销整个账号”。");
            return;
        }
        auto dialog = std::make_unique<Dialog>(window_, L"关闭" + GuiText::wide(data_.value("serviceName", "")));
        dialog->message(L"关闭后将删除该服务的账号、好友关系和群成员身份（群主身份会自动移交），无法恢复。")
            .field(L"请输入该服务的密码确认", L"", L"", true)
            .primary(L"确认关闭", [remote, &window](Dialog& d) {
                remote->call("account.closeService", json{ { "password", d.text(0) } }, [&window](bool ok, const json& data, const std::string& error) {
                    if (!ok) {
                        window.info(L"关闭本服务", GuiText::wide(error));
                        return;
                    }
                    const std::wstring message = GuiText::wide(data.value("message", ""));
                    // 这条连接已经回到未登录状态；会话对象正在执行这个回调，推迟到消息循环里再换掉它
                    window.post([&window, message] {
                        window.mainView().endNetworkSessions();
                        RemoteAccounts* accounts = window.remoteAccounts();
                        if (!accounts || !accounts->removeCurrent()) {
                            window.mainView().backToLogin(message);
                            return;
                        }
                        window.mainView().afterServiceChanged();
                        window.info(L"已关闭服务", message + L"\n当前服务已切换为 " + GuiText::wide(ServiceTypeHelper::name(window.session().me().type)) + L"。");
                    });
                });
                return true;
            }, true);
        window_.openDialog(std::move(dialog));
        break;
    }
    case ACT_DEREGISTER: {
        auto dialog = std::make_unique<Dialog>(window_, L"注销整个账号");
        dialog->message(L"注销将删除你在平台上的全部服务账号、好友关系和群成员身份，无法恢复。")
            .field(L"请输入当前服务的密码确认", L"", L"", true)
            .primary(L"确认注销", [remote, &window](Dialog& d) {
                remote->call("account.deregister", json{ { "password", d.text(0) } }, [&window](bool ok, const json& data, const std::string& error) {
                    if (!ok) {
                        window.info(L"注销整个账号", GuiText::wide(error));
                        return;
                    }
                    const std::wstring message = GuiText::wide(data.value("message", ""));
                    window.post([&window, message] { window.mainView().backToLogin(message); });
                });
                return true;
            }, true);
        window_.openDialog(std::move(dialog));
        break;
    }
    }
}

// ======================== 跨服务好友 ========================

NetCrossFriendPage::NetCrossFriendPage(GuiWindow& window) : Page(window) { load(); }

void NetCrossFriendPage::load() {
    RemoteSession* remote = window_.remoteSession();
    if (!remote) return;
    std::weak_ptr<int> alive = alive_;
    GuiWindow& window = window_;
    remote->call("friends.cross", json{ { "source", source_ } }, [this, alive, &window](bool ok, const json& data, const std::string& error) {
        if (alive.expired()) return;
        if (!ok) {
            error_ = error;
        } else if (source_.empty() && !data.value("sources", json::array()).empty()) {   // 第一次：默认选第一个其他服务
            source_ = data["sources"][0].get<std::string>();
            load();
            return;
        } else {
            data_ = data;
        }
        window.invalidate();
    });
}

void NetCrossFriendPage::build(PageModel& model) {
    const std::string current = ServiceTypeHelper::name(session().me().type);
    model.title = L"跨服务好友";
    model.subtitle = L"依据你在其他服务中的好友，在" + GuiText::wide(current) + L"中添加好友、查询共同好友";
    if (data_.is_null()) {
        model.note(error_.empty() ? L"正在查询……" : GuiText::wide(error_));
        return;
    }
    const json sources = data_.value("sources", json::array());
    if (sources.empty()) {
        model.note(L"你只开通了一个服务，请先开通其他服务。");
        return;
    }
    model.section(L"选择另一个服务");
    std::vector<PageModel::Button> choices;
    for (const json& s : sources) {
        const std::string code = s.get<std::string>();
        choices.push_back(PageModel::button(serviceName(code), ACT_SOURCE, code, code == source_ ? PageModel::PRIMARY : PageModel::NORMAL));
    }
    model.actions(choices);
    const std::wstring sourceName = serviceName(source_);
    model.section(L"推荐添加：你的" + sourceName + L"好友，在" + GuiText::wide(current) + L"中还不是好友");
    if (data_.contains("recommendError")) {
        model.note(GuiText::wide(data_.value("recommendError", "")));
        model.actions({ PageModel::button(L"去个人中心绑定QQ", ACT_ACCOUNT) });
    } else {
        const json list = data_.value("recommend", json::array());
        if (list.empty()) model.note(L"暂无可推荐的好友（你的" + sourceName + L"好友没有开通" + GuiText::wide(current) + L"、已经是你的好友，或无法确认是同一人）。");
        for (const json& r : list) {
            PageModel::Row& row = model.person(r.value("id", ""), GuiText::wide(r.value("nickname", "")),
                                               GuiText::wide(r.value("nickname", "") + "(" + r.value("id", "") + ")"),
                                               L"← " + sourceName + L"好友 " + GuiText::wide(r.value("sourceNickname", "") + "(" + r.value("sourceId", "") + ")"));
            row.buttons.push_back(PageModel::button(L"加好友", ACT_ADD, r.value("id", ""), PageModel::PRIMARY));
        }
        for (const json& s : data_.value("recommendSkipped", json::array())) model.note(L"（未推荐）" + GuiText::wide(s.get<std::string>()));
    }
    model.section(L"共同好友：在" + GuiText::wide(current) + L"和" + sourceName + L"中都是你的好友");
    if (data_.contains("commonError")) {
        model.note(GuiText::wide(data_.value("commonError", "")));
        return;
    }
    const json common = data_.value("common", json::array());
    if (common.empty()) model.note(L"暂无");
    for (const json& c : common) {
        model.person(c.value("id", ""), GuiText::wide(c.value("nickname", "")), GuiText::wide(c.value("name", "")),
                     GuiText::wide(current + " " + c.value("nickname", "") + "(" + c.value("id", "") + ")  /  ") + sourceName + L" " +
                         GuiText::wide(c.value("sourceNickname", "") + "(" + c.value("sourceId", "") + ")"));
    }
    for (const json& s : data_.value("commonSkipped", json::array())) model.note(L"（未计入）" + GuiText::wide(s.get<std::string>()));
}

void NetCrossFriendPage::onAction(int action, const std::string& arg) {
    switch (action) {
    case ACT_SOURCE:
        source_ = arg;
        data_ = json();
        load();
        break;
    case ACT_ADD: {   // 与微信相同：发送好友申请
        std::weak_ptr<int> alive = alive_;
        auto done = report();
        session().requestFriend(arg, "", "", [this, alive, done](bool ok, const std::string& message) {
            done(ok, message);
            if (!alive.expired()) load();
        });
        break;
    }
    case ACT_ACCOUNT:
        main().showAccount();
        break;
    }
}

// ======================== 实时群聊室 ========================

NetRoomPage::NetRoomPage(GuiWindow& window, int groupNo) : Page(window), groupNo_(groupNo), type_(window.session().me().type) {
    if (auto group = window.session().group(Conversation::groupChat(groupNo))) name_ = group->name;
    join();
}

NetRoomPage::~NetRoomPage() {
    if (RemoteSession* remote = session(); remote && joined_) remote->call("room.leave", json{ { "groupNo", groupNo_ } }, nullptr);
}

RemoteSession* NetRoomPage::session() {
    RemoteAccounts* accounts = window_.remoteAccounts();
    return accounts ? accounts->find(type_) : nullptr;
}

std::string NetRoomPage::key() const { return "G:" + std::to_string(groupNo_); }

int NetRoomPage::groupNo() const { return groupNo_; }

bool NetRoomPage::active() const { return joined_; }

void NetRoomPage::join() {
    RemoteSession* remote = session();
    if (!remote) return;
    std::weak_ptr<int> alive = alive_;
    remote->call("room.join", json{ { "groupNo", groupNo_ } }, [this, alive](bool ok, const json& data, const std::string& error) {
        if (alive.expired()) return;
        if (!ok) {
            lines_.push_back({ GuiText::wide(error), PageModel::CHAT_SYSTEM });
            joined_ = false;
        } else {
            name_ = data.value("name", name_);
            members_.clear();
            for (const json& m : data.value("members", json::array())) members_.push_back({ m.value("id", ""), m.value("nickname", "") });
            if (!joined_) lines_.push_back({ L"你进入了群聊室，现在室内 " + std::to_wstring(members_.size()) + L" 人。这里的消息由服务器转发、不保存，离开后就看不到了",
                                             PageModel::CHAT_SYSTEM });
            joined_ = true;
        }
        changed_ = true;
    });
}

void NetRoomPage::onEvent(const json& e) {
    if (e.value("groupNo", 0) != groupNo_) return;
    const std::string event = e.value("event", "");
    const std::string from = e.value("from", "");
    const std::string nickname = e.value("nickname", from);
    if (event == "join") {
        members_.push_back({ from, nickname });
        lines_.push_back({ GuiText::wide(nickname + " 进入了群聊室"), PageModel::CHAT_SYSTEM });
    } else if (event == "leave") {
        for (auto it = members_.begin(); it != members_.end(); ++it) {
            if (it->first == from) { members_.erase(it); break; }
        }
        lines_.push_back({ GuiText::wide(nickname + " 离开了群聊室"), PageModel::CHAT_SYSTEM });
    } else if (event == "say") {
        lines_.push_back({ GuiText::wide("[" + nickname + "] " + e.value("text", "")), PageModel::CHAT_OTHER });
    }
    changed_ = true;
}

bool NetRoomPage::poll() {
    // 断线后服务端已经把我们移出群聊室：连上后重新进入
    RemoteSession* remote = session();
    if (remote && joined_) {
        if (!remote->status().empty()) rejoin_ = true;
        else if (rejoin_) {
            rejoin_ = false;
            lines_.push_back({ L"连接恢复，重新进入群聊室（断线期间的发言收不到）", PageModel::CHAT_SYSTEM });
            join();
        }
    }
    const bool result = changed_;
    changed_ = false;
    return result;
}

void NetRoomPage::build(PageModel& model) {
    model.title = L"实时群聊室 · 群 " + std::to_wstring(groupNo_) + (name_.empty() ? L"" : L"（" + GuiText::wide(name_) + L"）");
    model.subtitle = joined_ ? std::to_wstring(members_.size()) + L" 人在室内 · 由服务器主持 · 内容不保存" : L"未进入";
    model.headerButtons.push_back(PageModel::button(L"成员", ACT_MEMBERS));
    if (joined_) model.headerButtons.push_back(PageModel::button(L"离开", ACT_LEAVE, "", PageModel::DANGER));
    for (const auto& line : lines_) model.chat(line.first, static_cast<PageModel::ChatKind>(line.second));
    if (!joined_) model.actions({ PageModel::button(L"进入群聊室", ACT_JOIN, "", PageModel::PRIMARY) });
    model.composer = joined_;
    model.stickBottom = true;
}

void NetRoomPage::onAction(int action, const std::string&) {
    RemoteSession* remote = session();
    switch (action) {
    case ACT_MEMBERS: {
        std::vector<std::wstring> names;
        for (const auto& m : members_) names.push_back(GuiText::wide(m.second + "(" + m.first + ")"));
        auto dialog = std::make_unique<Dialog>(window_, L"群聊室里的成员");
        dialog->message(L"共 " + std::to_wstring(names.size()) + L" 人").lines(names).cancelText(L"关闭");
        window_.openDialog(std::move(dialog));
        break;
    }
    case ACT_LEAVE:
        if (remote && joined_) remote->call("room.leave", json{ { "groupNo", groupNo_ } }, nullptr);
        joined_ = false;
        lines_.push_back({ L"你离开了群聊室", PageModel::CHAT_SYSTEM });
        break;
    case ACT_JOIN:
        join();
        break;
    }
}

void NetRoomPage::onCompose(const std::string& text) {
    RemoteSession* remote = session();
    if (!remote || !joined_) return;
    std::weak_ptr<int> alive = alive_;
    GuiWindow& window = window_;
    remote->call("room.say", json{ { "groupNo", groupNo_ }, { "text", text } }, [this, alive, &window, text](bool ok, const json&, const std::string& error) {
        if (!ok) {
            window.mainView().toast(GuiText::wide(error));
            return;
        }
        if (alive.expired()) return;
        lines_.push_back({ GuiText::wide(text), PageModel::CHAT_MINE });
        changed_ = true;
    });
}

// ======================== 点对点聊天 ========================

namespace {
constexpr unsigned long long kInviteTimeoutMs = 60000;   // 邀请这么久没有回应就取消
constexpr unsigned long long kDirectTimeoutMs = 8000;    // 这么久还没直连上就改由服务端中转
}  // namespace

NetP2PPage::NetP2PPage(GuiWindow& window) : Page(window), log_(window.platform().dataDir() + "chat_log.txt") {}

NetP2PPage::~NetP2PPage() { end("", true); }

bool NetP2PPage::busy() const { return state_ != IDLE && state_ != ENDED; }

void NetP2PPage::addLine(const std::string& text, int kind) {
    lines_.push_back({ GuiText::wide(text), kind });
    changed_ = true;
}

void NetP2PPage::signal(const json& data) {
    RemoteAccounts* accounts = window_.remoteAccounts();
    RemoteSession* remote = accounts ? accounts->find(type_) : nullptr;
    if (!remote) return;
    std::weak_ptr<int> alive = alive_;
    const std::string kind = data.value("kind", "");
    remote->call("p2p.signal", json{ { "to", peer_ }, { "data", data } }, [this, alive, kind](bool ok, const json&, const std::string& error) {
        if (ok || alive.expired() || kind == "end" || kind == "reject") return;
        end(error, false);   // 对方下线了、不再是好友……
    });
}

json NetP2PPage::hostInfo() {
    const std::string key = Password::randomHex(8);   // 一次性口令：别人扫到这个端口也连不进来
    std::string error;
    if (!link_.host(key, error)) {
        addLine("开端口等待对方连接失败（" + error + "），将改由服务器中转", PageModel::CHAT_SYSTEM);
        return json();
    }
    return json{ { "ips", DirectLink::localAddresses() }, { "port", link_.port() }, { "key", key } };
}

void NetP2PPage::invite(RemoteSession& session, const std::string& peer) {
    if (busy()) return;
    lines_.clear();
    type_ = session.me().type;
    peer_ = peer;
    peerName_ = session.nicknameOf(peer);
    id_ = Password::randomHex(8);
    state_ = INVITING;
    deadline_ = now() + kInviteTimeoutMs;
    json data = { { "kind", "invite" }, { "session", id_ }, { "rtc", false } };
    const json host = hostInfo();   // 电脑这边开端口等待：对方是电脑就用 TCP 连过来，是浏览器就用 WebSocket
    if (!host.is_null()) data["host"] = host;
    addLine("正在邀请 " + peerName_ + " 点对点聊天，等待对方接受……", PageModel::CHAT_SYSTEM);
    signal(data);
}

void NetP2PPage::accept(RemoteSession& session, const std::string& from, const std::string& nickname, const json& data) {
    end("", false);
    lines_.clear();
    type_ = session.me().type;
    peer_ = from;
    peerName_ = nickname.empty() ? from : nickname;
    id_ = data.value("session", "");
    state_ = CONNECTING;
    deadline_ = now() + kDirectTimeoutMs;
    json reply = { { "kind", "accept" }, { "session", id_ }, { "rtc", false } };
    const json host = data.value("host", json());
    if (host.is_object()) {   // 对方是电脑、开了端口：连过去，几个地址同时试
        std::string error;
        const unsigned short port = static_cast<unsigned short>(host.value("port", 0));
        if (!link_.connect(host.value("ips", std::vector<std::string>()), port, host.value("key", ""), error)) {
            addLine(error, PageModel::CHAT_SYSTEM);
        } else {
            addLine("正在直连对方的电脑（端口 " + std::to_string(port) + "）……", PageModel::CHAT_SYSTEM);
        }
    } else {   // 对方是浏览器（不能开端口）：我们开端口等它连过来
        const json mine = hostInfo();
        if (!mine.is_null()) reply["host"] = mine;
        addLine("正在等待对方连接过来……", PageModel::CHAT_SYSTEM);
    }
    signal(reply);
}

void NetP2PPage::onSignal(RemoteSession&, const json& signal) {
    const json data = signal.value("data", json::object());
    if (signal.value("from", "") != peer_ || data.value("session", "") != id_ || !busy()) return;
    const std::string kind = data.value("kind", "");
    if (kind == "accept" && state_ == INVITING) {
        state_ = CONNECTING;
        deadline_ = now() + kDirectTimeoutMs;
        addLine(peerName_ + " 接受了邀请，正在建立直连……", PageModel::CHAT_SYSTEM);
        const json host = data.value("host", json());
        if (link_.state() != DirectLink::LISTENING && host.is_object()) {   // 我们没能开端口，对方开了：连过去
            std::string error;
            link_.connect(host.value("ips", std::vector<std::string>()), static_cast<unsigned short>(host.value("port", 0)), host.value("key", ""), error);
        }
    } else if (kind == "reject") {
        end(data.value("reason", "对方拒绝了邀请"), false);
    } else if (kind == "relay" && state_ == CONNECTING) {
        useRelay("对方那边直连没有成功");
    } else if (kind == "msg" && state_ == RELAY) {
        const std::string text = data.value("text", "");
        addLine("[" + peerName_ + "] " + text, PageModel::CHAT_OTHER);
        log_.append("收", peer_, text);
    } else if (kind == "end") {
        end("对方结束了聊天", false);
    }
}

void NetP2PPage::useRelay(const std::string& reason) {
    link_.close();
    state_ = RELAY;
    addLine(reason + "。改由服务器中转（只转发、不保存）", PageModel::CHAT_SYSTEM);
}

void NetP2PPage::end(const std::string& reason, bool notify) {
    if (!busy()) return;
    if (notify) signal(json{ { "kind", "end" }, { "session", id_ } });
    link_.close();
    state_ = ENDED;
    if (!reason.empty()) addLine(reason, PageModel::CHAT_SYSTEM);
}

bool NetP2PPage::poll() {
    for (const std::string& line : link_.poll()) {   // 对方发来的内容已带有“[昵称] ”前缀
        addLine(line, PageModel::CHAT_OTHER);
        log_.append("收", peer_, line);
    }
    if (state_ == CONNECTING && link_.state() == DirectLink::CONNECTED) {
        state_ = DIRECT;
        addLine(std::string("已直连（") + (link_.webSocket() ? "对方用浏览器，WebSocket " : "TCP ") + link_.peer() + "），消息不经过服务器",
                PageModel::CHAT_SYSTEM);
    } else if (state_ == DIRECT && link_.state() == DirectLink::CLOSED) {
        end(link_.error(), true);
    } else if (state_ == CONNECTING && now() > deadline_) {
        signal(json{ { "kind", "relay" }, { "session", id_ } });
        useRelay("直连没有成功（可能不在同一个网络，或者被防火墙拦住了）");
    } else if (state_ == INVITING && now() > deadline_) {
        end("对方没有回应", true);
    }
    const bool result = changed_;
    changed_ = false;
    return result;
}

void NetP2PPage::onCompose(const std::string& text) {
    if (state_ == DIRECT) {
        if (!link_.send("[" + session().me().nickname + "] " + text)) {   // 与单机版、控制台版相同的格式
            end(link_.error(), true);
            return;
        }
    } else if (state_ == RELAY) {
        signal(json{ { "kind", "msg" }, { "session", id_ }, { "text", text } });
    } else {
        return;
    }
    addLine(text, PageModel::CHAT_MINE);
    log_.append("发", peer_, text);
}

void NetP2PPage::build(PageModel& model) {
    model.title = L"点对点聊天" + (peerName_.empty() ? std::wstring() : L" · " + GuiText::wide(peerName_));
    switch (state_) {
    case IDLE: model.subtitle = L"选一位在线的好友直接连接，消息不经过服务器"; break;
    case INVITING: model.subtitle = L"等待对方接受……"; break;
    case CONNECTING: model.subtitle = L"正在建立直连……"; break;
    case DIRECT: model.subtitle = L"已直连 " + GuiText::wide(link_.peer()) + L" · 不经过服务器"; break;
    case RELAY: model.subtitle = L"直连不通，经服务器中转（不保存）"; break;
    case ENDED: model.subtitle = L"已结束"; break;
    }
    if (busy()) model.headerButtons.push_back(PageModel::button(state_ == INVITING ? L"取消邀请" : L"结束聊天", ACT_STOP, "", PageModel::DANGER));
    for (const auto& line : lines_) model.chat(line.first, static_cast<PageModel::ChatKind>(line.second));
    if (state_ == DIRECT || state_ == RELAY) {
        model.composer = true;
        model.stickBottom = true;
        return;   // 聊天中不显示好友列表和历史记录
    }
    if (busy()) return;
    if (state_ == ENDED) model.actions({ PageModel::button(L"再次邀请 " + GuiText::wide(peerName_), ACT_AGAIN, peer_, PageModel::PRIMARY) });

    if (lines_.empty()) {
        model.text(L"联网后不用再手动输入对方的 IP 和端口：服务器帮双方交换连接信息，聊天内容直接在两台设备之间传送，不经过服务器。\n"
                   L"对方用电脑客户端或手机浏览器都可以；直连不通（比如不在同一个网络）时自动改由服务器中转，只转发、不保存。");
    }
    model.section(L"在线的好友");
    bool any = false;
    for (const Conversation& c : session().contacts()) {
        const ProfileView p = session().profile(c.peerId);
        if (!p.online) continue;
        any = true;
        PageModel::Row& row = model.person(c.peerId, GuiText::wide(c.avatarName), GuiText::wide(c.title), GuiText::wide(c.peerId));
        row.tags.push_back({ L"在线", 0x23C343 });
        row.buttons.push_back(PageModel::button(L"直连聊天", ACT_INVITE, c.peerId, PageModel::PRIMARY));
    }
    if (!any) model.note(L"现在没有在线的好友");

    // 聊天记录（data\chat_log.txt，与单机版共用）
    model.section(L"聊天记录");
    model.actions({ PageModel::button(L"最近 20 条", ACT_RECENT), PageModel::button(L"按关键字查找", ACT_SEARCH) });
    if (!historyTitle_.empty()) {
        std::wstring text;
        for (const std::string& line : history_) text += (text.empty() ? L"" : L"\n") + GuiText::wide(line);
        model.note(historyTitle_);
        if (!history_.empty()) model.text(text);
    }
}

void NetP2PPage::onAction(int action, const std::string& arg) {
    switch (action) {
    case ACT_INVITE:
    case ACT_AGAIN:
        if (RemoteSession* remote = window_.remoteSession()) invite(*remote, arg);
        break;
    case ACT_STOP:
        end(state_ == INVITING ? "已取消邀请" : "聊天结束。", true);
        break;
    case ACT_RECENT:
        history_ = log_.query("", 20);
        historyTitle_ = history_.empty() ? L"没有聊天记录。" : L"最近 " + std::to_wstring(history_.size()) + L" 条：";
        break;
    case ACT_SEARCH: {
        auto dialog = std::make_unique<Dialog>(window_, L"查找聊天记录");
        dialog->field(L"关键字", L"", L"对方账号、消息内容……").primary(L"查找", [this](Dialog& d) {
            const std::string keyword = d.text(0);
            history_ = log_.query(keyword, 20);
            historyTitle_ = history_.empty() ? L"没有找到包含“" + GuiText::wide(keyword) + L"”的聊天记录。"
                                             : L"包含“" + GuiText::wide(keyword) + L"”的记录共 " + std::to_wstring(history_.size()) + L" 条：";
            window_.invalidate();
            return true;
        });
        window_.openDialog(std::move(dialog));
        break;
    }
    }
}

// ======================== 对话框 ========================

void NetTools::showOverview(GuiWindow& window) {
    RemoteSession* remote = window.remoteSession();
    if (!remote) return;
    remote->call("platform.overview", json::object(), [&window](bool ok, const json& data, const std::string& error) {
        if (!ok) {
            window.info(L"平台概况", GuiText::wide(error));
            return;
        }
        std::vector<std::wstring> lines;
        for (const json& u : data.value("users", json::array())) {
            std::string line = "用户" + std::to_string(u.value("id", 0)) + "  " + u.value("name", "") + "：";
            for (const json& s : u.value("services", json::array())) {
                ServiceType type = ServiceType::QQ;
                ServiceTypeHelper::parse(s.value("service", ""), type);
                line += "  " + ServiceTypeHelper::name(type) + "[" + s.value("account", "") + "]";
            }
            lines.push_back(GuiText::wide(line));
        }
        std::wstring groups = L"群数量：";
        // 先存下来：value() 返回临时对象，直接对它的 items() 做 range-for，临时对象在循环开始前就销毁了
        const json counts = data.value("groups", json::object());
        for (const auto& item : counts.items())
            groups += serviceName(item.key()) + L" " + std::to_wstring(item.value().get<int>()) + L" 个  ";
        lines.push_back(groups);
        lines.push_back(L"（预置演示账号的密码均为 123456）");
        auto dialog = std::make_unique<Dialog>(window, L"平台概况");
        dialog->message(L"共 " + std::to_wstring(data.value("users", json::array()).size()) + L" 位用户（服务器上的数据）").lines(lines).cancelText(L"关闭");
        window.openDialog(std::move(dialog));
    });
}

void NetTools::openServiceDialog(GuiWindow& window) {
    RemoteSession* remote = window.remoteSession();
    if (!remote) return;
    remote->call("account.get", json::object(), [&window](bool ok, const json& data, const std::string& error) {
        if (!ok) {
            window.info(L"开通新服务", GuiText::wide(error));
            return;
        }
        std::vector<std::string> codes = data.value("available", std::vector<std::string>());
        if (codes.empty()) {
            window.info(L"开通新服务", L"你已开通平台上的全部服务。");
            return;
        }
        std::vector<std::wstring> names;
        for (const std::string& code : codes) names.push_back(serviceName(code));
        auto dialog = std::make_unique<Dialog>(window, L"开通新服务");
        dialog->choice(L"服务", names, 0);
        addProfileFields(*dialog);
        dialog->choice(L"与本人的 QQ 号绑定（仅微信、微商有效）", { L"绑定", L"不绑定" }, 0);
        dialog->primary(L"开通", [&window, codes](Dialog& d) {
            json args = { { "service", codes[static_cast<size_t>(d.selected(0))] }, { "bindQQ", d.selected(1) == 0 } };
            if (!readProfile(d, 0, args)) return false;
            RemoteSession* current = window.remoteSession();
            if (!current) return true;
            current->call("account.openService", args, [&window](bool ok, const json& data, const std::string& error) {
                if (!ok) {
                    window.info(L"开通新服务", GuiText::wide(error));
                    return;
                }
                ServiceType type = ServiceType::QQ;
                if (RemoteAccounts* accounts = window.remoteAccounts(); accounts && ServiceTypeHelper::parse(data.value("service", ""), type))
                    accounts->addService(type, data.value("account", ""));   // 一处登录：凭令牌登录新开通的服务
                window.info(L"开通成功", GuiText::wide(data.value("message", "")) + L"\n已自动登录，可以在左侧导航栏切换过去。");
                window.mainView().reload();
            });
            return true;
        });
        window.openDialog(std::move(dialog));
    });
}

namespace {

// 还没有登录时调用一个不需要登录的操作（注册）：临时建一条连接，发一个请求，等它的响应。最多等几秒，界面短暂停顿
bool blockingCall(const std::string& server, const std::string& op, const json& args, json& data, std::string& error) {
    const size_t colon = server.rfind(':');
    std::string host = colon == std::string::npos ? server : server.substr(0, colon);
    int port = 9000;
    try {
        if (colon != std::string::npos) port = std::stoi(server.substr(colon + 1));
    } catch (...) {
        error = "服务器地址格式不对，应为“IP:端口”";
        return false;
    }
    if (host.empty() || host == "localhost") host = "127.0.0.1";
    SocketOps::startup();
    bool inProgress = false;
    SocketHandle s = SocketOps::startConnect(host, static_cast<std::uint16_t>(port), inProgress);
    if (s == kInvalidSocket) {
        error = "服务器地址无效：" + server;
        return false;
    }
    auto waitFor = [&](bool write, int ms) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(s, &set);
        timeval timeout{ ms / 1000, (ms % 1000) * 1000 };
        return select(0, write ? nullptr : &set, write ? &set : nullptr, nullptr, &timeout) > 0;
    };
    if (inProgress && (!waitFor(true, 3000) || SocketOps::socketError(s) != 0)) {
        SocketOps::close(s);
        error = "无法连接服务器 " + server + "，请确认服务端已启动、地址正确";
        return false;
    }
    Packet packet;
    packet.type = MessageType::CALL_REQ;
    packet.body = { { "reqId", 1 }, { "op", op }, { "args", args } };
    const std::string bytes = packet.encode();
    for (size_t sent = 0; sent < bytes.size();) {
        const long n = SocketOps::sendSome(s, bytes.data() + sent, bytes.size() - sent);
        if (n > 0) sent += static_cast<size_t>(n);
        else if (!waitFor(true, 3000)) break;
    }
    std::string buffer;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        Frame frame;
        size_t consumed = 0;
        if (Frame::decode(buffer.data(), buffer.size(), frame, consumed) == Frame::DecodeResult::OK) {
            SocketOps::close(s);
            std::optional<Packet> response = Packet::fromFrame(frame);
            if (!response || response->type != MessageType::CALL_RESP) {
                error = "服务器的响应不正确";
                return false;
            }
            if (!response->body.value("ok", false)) {
                error = response->body.value("error", "操作失败");
                return false;
            }
            data = response->body.value("data", json::object());
            return true;
        }
        if (!waitFor(false, 500)) continue;
        char chunk[4096];
        const long n = SocketOps::recvSome(s, chunk, sizeof(chunk));
        if (n <= 0) break;
        buffer.append(chunk, static_cast<size_t>(n));
    }
    SocketOps::close(s);
    error = "服务器没有响应";
    return false;
}

}  // namespace

void NetTools::registerDialog(GuiWindow& window, const std::string& server) {
    std::vector<ServiceType> all = ServiceTypeHelper::all();
    std::vector<std::wstring> names;
    for (ServiceType t : all) names.push_back(GuiText::wide(ServiceTypeHelper::name(t)));
    auto dialog = std::make_unique<Dialog>(window, L"注册新用户（服务器 " + GuiText::wide(server) + L"）");
    dialog->field(L"真实姓名", L"", L"请输入真实姓名").choice(L"首个开通的服务", names, 0);
    addProfileFields(*dialog);
    dialog->primary(L"注册", [&window, all, server](Dialog& d) {
        if (d.text(0).empty()) {
            d.setError(L"真实姓名不能为空。");
            return false;
        }
        const ServiceType type = all[static_cast<size_t>(d.selected(0))];
        json args = { { "name", d.text(0) }, { "service", ServiceTypeHelper::code(type) } };
        if (!readProfile(d, 1, args)) return false;
        json data;
        std::string error;
        if (!blockingCall(server, "account.register", args, data, error)) {
            d.setError(GuiText::wide(error));
            return false;
        }
        window.loginView().prefill(type, data.value("account", ""));
        window.info(L"注册成功", GuiText::wide(data.value("message", "")) + L"\n已为你填好账号，输入密码即可登录。");
        return true;
    });
    window.openDialog(std::move(dialog));
}
