#include "LoginView.h"
#include <cstdlib>
#include "Canvas.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "MainView.h"
#include "PlatformTools.h"
#include "Theme.h"
#include "Util.h"

namespace {
const float kCardWidth = 400.0f;
const float kCardHeight = 510.0f;
const float kServerRowHeight = 82.0f;                 // 联网模式多出的“服务器”一行
const unsigned long long kConnectTimeoutMs = 8000;   // 联网登录多久没有结果就放弃
const std::uint16_t kDefaultPort = 9000;
}  // namespace

LoginView::LoginView(GuiWindow& window) : View(window) {}

ServiceType LoginView::service() const { return ServiceTypeHelper::all()[serviceIndex_]; }

void LoginView::show() {
    error_.clear();
    notice_.clear();
    connecting_ = false;
    account_ = window_.createEdit(false, false, L"请输入账号", Theme::FIELD);
    password_ = window_.createEdit(false, true, L"请输入密码", Theme::FIELD);
    server_ = window_.createEdit(false, false, L"服务器地址，如 127.0.0.1:9000", Theme::FIELD);
    SetWindowTextW(server_, serverText_.c_str());
    ShowWindow(server_, remote_ ? SW_SHOW : SW_HIDE);
    GuiWindow::focus(account_);
}

void LoginView::setNotice(const std::wstring& text) {
    notice_ = text;
    error_.clear();
    window_.invalidate();
}

void LoginView::selectService(ServiceType type) {
    std::vector<ServiceType> all = ServiceTypeHelper::all();
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i] == type) serviceIndex_ = static_cast<int>(i);
    }
}

void LoginView::prefill(ServiceType type, const std::string& id) {
    selectService(type);
    if (account_) SetWindowTextW(account_, GuiText::wide(id).c_str());
    if (password_) SetWindowTextW(password_, L"");
    error_.clear();
    notice_ = L"注册成功，请输入密码登录。";
    window_.post([this] { GuiWindow::focus(password_); });   // 对话框关闭之后再把焦点放到密码框
    window_.invalidate();
}

void LoginView::hide() {
    window_.destroyEdit(account_);
    window_.destroyEdit(password_);
    window_.destroyEdit(server_);
}

D2D1_RECT_F LoginView::card() const {
    const float height = kCardHeight + (remote_ ? kServerRowHeight : 0.0f);
    const float x = (bounds_.left + bounds_.right - kCardWidth) / 2;
    const float y = (bounds_.top + bounds_.bottom - height) / 2 - 12;
    return D2D1::RectF(x, y, x + kCardWidth, y + height);
}

D2D1_RECT_F LoginView::field(int row) const {
    const D2D1_RECT_F c = card();
    const float top = c.top + 272.0f + 82.0f * row;
    return D2D1::RectF(c.left + 36, top, c.right - 36, top + 46);
}

void LoginView::layout(const D2D1_RECT_F& bounds) {
    View::layout(bounds);
    HWND edits[] = { account_, password_, server_ };
    for (int row = 0; row < 3; ++row) {
        const D2D1_RECT_F f = field(row);
        const float mid = (f.top + f.bottom) / 2;
        if (edits[row]) window_.placeEdit(edits[row], D2D1::RectF(f.left + 16, mid - 11, f.right - 16, mid + 11));
    }
}

void LoginView::paint(Canvas& c) {
    beginHits();
    const D2D1_COLOR_F accent = Theme::accent(service());
    const D2D1_RECT_F all = bounds_;

    // 背景：浅色渐变，加两个很淡的强调色圆形作点缀
    c.verticalGradient(all, Theme::rgb(0xEDF1F8), Theme::rgb(0xF7F8FA));
    D2D1_COLOR_F glow = accent;
    glow.a = 0.07f;
    c.fillCircle(all.right - 140, all.top + 40, 280, glow);
    glow.a = 0.05f;
    c.fillCircle(all.left + 60, all.bottom - 20, 220, glow);

    const D2D1_RECT_F card = this->card();
    const float cx = (card.left + card.right) / 2;
    c.shadow(card, 20, 26, 0.10f);
    c.fillRound(card, 20, Theme::rgb(Theme::PANEL));

    // 标志与标题
    const D2D1_RECT_F logo = D2D1::RectF(cx - 28, card.top + 40, cx + 28, card.top + 96);
    c.fillRound(logo, 16, accent);
    c.text(L"IM", logo, 22, Theme::rgb(0xFFFFFF), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.text(L"IMChat", D2D1::RectF(card.left, card.top + 110, card.right, card.top + 140), 22,
           Theme::rgb(Theme::TEXT), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.text(L"即时通信 · QQ / 微信 / 微博 / 微商", D2D1::RectF(card.left, card.top + 142, card.right, card.top + 164), 13,
           Theme::rgb(Theme::TEXT3), Align::CENTER);

    // 右上角：单机 / 联网
    const D2D1_RECT_F mode = D2D1::RectF(card.right - 128, card.top + 18, card.right - 18, card.top + 46);
    c.fillRound(mode, 8, Theme::rgb(Theme::FIELD));
    const wchar_t* modes[] = { L"单机", L"联网" };
    for (int i = 0; i < 2; ++i) {
        const float half = (mode.right - mode.left) / 2;
        const D2D1_RECT_F r = D2D1::RectF(mode.left + half * i, mode.top, mode.left + half * (i + 1), mode.bottom);
        const bool selected = (i == 1) == remote_;
        if (selected) c.fillRound(D2D1::RectF(r.left + 2, r.top + 2, r.right - 2, r.bottom - 2), 6, Theme::rgb(Theme::PANEL));
        c.text(modes[i], r, 12.5f, selected ? accent : Theme::rgb(Theme::TEXT3), Align::CENTER,
               selected ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        if (!connecting_) addHit(r, ACT_MODE, i);
    }

    // 服务选择（分段按钮）
    const D2D1_RECT_F seg = D2D1::RectF(card.left + 36, card.top + 190, card.right - 36, card.top + 230);
    c.fillRound(seg, 10, Theme::rgb(Theme::FIELD));
    std::vector<ServiceType> services = ServiceTypeHelper::all();
    const float segWidth = (seg.right - seg.left) / services.size();
    for (size_t i = 0; i < services.size(); ++i) {
        const D2D1_RECT_F r = D2D1::RectF(seg.left + segWidth * i, seg.top, seg.left + segWidth * (i + 1), seg.bottom);
        const bool selected = static_cast<int>(i) == serviceIndex_;
        if (selected) {
            const D2D1_RECT_F pill = D2D1::RectF(r.left + 3, r.top + 3, r.right - 3, r.bottom - 3);
            c.shadow(pill, 8, 4, 0.06f);
            c.fillRound(pill, 8, Theme::rgb(Theme::PANEL));
        }
        c.text(Theme::shortName(services[i]), r, 14,
               selected ? accent : Theme::rgb(hovered(r) ? Theme::TEXT : Theme::TEXT2), Align::CENTER,
               selected ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        addHit(r, ACT_SERVICE, static_cast<int>(i));
    }

    // 账号、密码；联网模式还有服务器地址
    const wchar_t* labels[] = { L"账号", L"密码", L"服务器" };
    HWND edits[] = { account_, password_, server_ };
    const int rows = remote_ ? 3 : 2;
    for (int row = 0; row < rows; ++row) {
        const D2D1_RECT_F f = field(row);
        c.text(labels[row], D2D1::RectF(f.left + 2, f.top - 22, f.right, f.top - 4), 13, Theme::rgb(Theme::TEXT2));
        c.fillRound(f, 12, Theme::rgb(Theme::FIELD));
        if (GetFocus() == edits[row]) c.strokeRound(f, 12, accent, 1.5f);
        addHit(f, row == 0 ? ACT_ACCOUNT : row == 1 ? ACT_PASSWORD : ACT_SERVER);
    }
    const float extra = remote_ ? kServerRowHeight : 0.0f;
    if (!error_.empty())
        c.text(error_, D2D1::RectF(card.left + 20, card.top + 406 + extra, card.right - 20, card.top + 428 + extra), 13,
               Theme::rgb(Theme::DANGER), Align::CENTER);
    else if (!notice_.empty())
        c.text(notice_, D2D1::RectF(card.left + 20, card.top + 406 + extra, card.right - 20, card.top + 428 + extra), 13,
               Theme::rgb(0x00A870), Align::CENTER);

    // 登录按钮（联网登录进行中显示进度）
    const D2D1_RECT_F button = D2D1::RectF(card.left + 36, card.top + 434 + extra, card.right - 36, card.top + 482 + extra);
    D2D1_COLOR_F fill = hovered(button) && !connecting_ ? Theme::darker(accent, 0.08f) : accent;
    if (connecting_) fill.a = 0.6f;
    c.fillRound(button, 12, fill);
    std::wstring label = L"登  录";
    if (connecting_) {
        RemoteSession* session = window_.remoteSession();
        const bool loading = session && session->loginState() == RemoteSession::LoginState::LOADING;
        label = loading ? L"正在加载数据…" : L"正在连接服务器…";
    }
    c.text(label, button, 16, Theme::rgb(0xFFFFFF), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    if (!connecting_) addHit(button, ACT_LOGIN);

    if (remote_) {   // 注册、概况、保存都是对本地数据的操作，联网模式下不显示
        c.text(L"联网模式：先启动 IMChatServer（默认端口 9000），不同电脑填服务端所在电脑的 IP",
               D2D1::RectF(all.left, card.bottom + 20, all.right, card.bottom + 42), 12, Theme::rgb(Theme::TEXT3), Align::CENTER);
        c.text(L"演示账号：QQ 10001 · 10002 · 10003，密码均为 123456",
               D2D1::RectF(all.left, card.bottom + 46, all.right, card.bottom + 68), 12, Theme::rgb(Theme::TEXT3), Align::CENTER);
        return;
    }

    // 卡片下方：控制台主菜单的另外三项
    const wchar_t* links[] = { L"注册新用户", L"平台概况", L"立即保存数据" };
    float widths[3], total = 0;
    for (int i = 0; i < 3; ++i) total += (widths[i] = c.textWidth(links[i], 13.5f) + 16);
    total += 2 * 20;
    float x = (all.left + all.right - total) / 2;
    for (int i = 0; i < 3; ++i) {
        const D2D1_RECT_F link = D2D1::RectF(x, card.bottom + 16, x + widths[i], card.bottom + 44);
        if (hovered(link)) c.fillRound(link, 8, Theme::rgb(0xFFFFFF, 0.8f));
        c.text(links[i], link, 13.5f, accent, Align::CENTER);
        addHit(link, ACT_LINK, i);
        x += widths[i];
        if (i < 2) c.fillCircle(x + 10, (link.top + link.bottom) / 2, 1.6f, Theme::rgb(Theme::TEXT4));
        x += 20;
    }
    c.text(L"演示账号：QQ 10001 · 微信 wx2001 · 微商 ws3001，密码均为 123456",
           D2D1::RectF(all.left, card.bottom + 50, all.right, card.bottom + 72), 12, Theme::rgb(Theme::TEXT3),
           Align::CENTER);
}

void LoginView::onAction(int action, int index) {
    switch (action) {
    case ACT_SERVICE:
        serviceIndex_ = index;
        error_.clear();
        GuiWindow::focus(account_);
        break;
    case ACT_ACCOUNT: GuiWindow::focus(account_); break;
    case ACT_PASSWORD: GuiWindow::focus(password_); break;
    case ACT_SERVER: GuiWindow::focus(server_); break;
    case ACT_MODE:
        remote_ = index == 1;
        error_.clear();
        notice_.clear();
        ShowWindow(server_, remote_ ? SW_SHOW : SW_HIDE);
        layout(bounds_);   // 卡片变高或变矮，输入框跟着移动
        break;
    case ACT_LOGIN: submit(); return;   // 登录成功后本视图已经隐藏，不再重绘
    case ACT_LINK:
        if (index == LINK_REGISTER) PlatformTools::registerDialog(window_);
        if (index == LINK_OVERVIEW) PlatformTools::showOverview(window_);
        if (index == LINK_SAVE) PlatformTools::saveNow(window_);
        break;
    }
    window_.invalidate();
}

bool LoginView::onEditKey(HWND edit, int key) {
    if (key == VK_TAB) {
        HWND next = edit == account_ ? password_ : (edit == password_ && remote_ ? server_ : account_);
        GuiWindow::focus(next);
        return true;
    }
    if (key == VK_RETURN) {
        if (edit == account_) GuiWindow::focus(password_);
        else window_.submitLoginLater();
        return true;
    }
    return false;
}

void LoginView::onTimer() {
    if (!connecting_) return;
    RemoteSession* session = window_.remoteSession();
    if (!session) {
        connecting_ = false;
        return;
    }
    const RemoteSession::LoginState state = session->loginState();
    if (state == RemoteSession::LoginState::READY) {
        connecting_ = false;
        window_.showMain();
        window_.mainView().toast(L"已连接服务器 " + GuiText::wide(session->server()));
    } else if (state == RemoteSession::LoginState::FAILED) {
        remoteFailed(GuiText::wide(session->loginError()));
    } else if (GetTickCount64() - connectStarted_ > kConnectTimeoutMs) {
        remoteFailed(L"无法连接服务器 " + GuiText::wide(session->server()) + L"，请确认服务端已启动、地址正确");
    }
    window_.invalidate();   // 按钮上的进度文字
}

void LoginView::startRemote(const std::string& server, const std::string& id, const std::string& password) {
    std::string host = server;
    std::uint16_t port = kDefaultPort;
    const std::size_t colon = server.rfind(':');
    if (colon != std::string::npos) {
        host = server.substr(0, colon);
        port = static_cast<std::uint16_t>(std::atoi(server.c_str() + colon + 1));
    }
    if (host.empty() || port == 0) {
        error_ = L"服务器地址格式应为 IP:端口，如 127.0.0.1:9000";
        window_.invalidate();
        return;
    }
    serverText_ = GuiText::wide(server);
    auto accounts = std::make_unique<RemoteAccounts>(host, port);
    accounts->start(service(), id, password);   // 登录成功后，本人开通的其他服务凭令牌自动登录
    window_.startRemote(std::move(accounts));
    connecting_ = true;
    connectStarted_ = GetTickCount64();
    error_.clear();
    notice_.clear();
    window_.invalidate();
}

void LoginView::remoteFailed(const std::wstring& error) {
    connecting_ = false;
    window_.endRemote();   // 停止后台的连接和重试
    error_ = error;
    SetWindowTextW(password_, L"");
    GuiWindow::focus(password_);
}

void LoginView::autoRemote(const std::string& server, ServiceType type, const std::string& id, const std::string& password) {
    remote_ = true;
    selectService(type);
    SetWindowTextW(server_, GuiText::wide(server).c_str());
    SetWindowTextW(account_, GuiText::wide(id).c_str());
    ShowWindow(server_, SW_SHOW);
    layout(bounds_);
    startRemote(server, id, password);
}

void LoginView::submit() {
    if (connecting_) return;
    const std::string id = Util::trim(GuiText::utf8(GuiText::editText(account_)));
    const std::string password = Util::trim(GuiText::utf8(GuiText::editText(password_)));
    if (remote_) {
        if (id.empty() || password.empty()) {
            error_ = L"请输入账号和密码。";
            window_.invalidate();
            return;
        }
        startRemote(Util::trim(GuiText::utf8(GuiText::editText(server_))), id, password);
        return;
    }
    std::string msg;
    if (!window_.localSession().login(service(), id, password, msg)) {
        error_ = GuiText::wide(msg);
        SetWindowTextW(password_, L"");
        GuiWindow::focus(password_);
        window_.invalidate();
        return;
    }
    window_.showMain();
    if (!msg.empty()) window_.mainView().toast(GuiText::wide(msg));
}
