#include "LoginView.h"
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
}  // namespace

LoginView::LoginView(GuiWindow& window) : View(window) {}

ServiceType LoginView::service() const { return ServiceTypeHelper::all()[serviceIndex_]; }

void LoginView::show() {
    error_.clear();
    notice_.clear();
    account_ = window_.createEdit(false, false, L"请输入账号", Theme::FIELD);
    password_ = window_.createEdit(false, true, L"请输入密码", Theme::FIELD);
    GuiWindow::focus(account_);
}

void LoginView::setNotice(const std::wstring& text) {
    notice_ = text;
    error_.clear();
    window_.invalidate();
}

void LoginView::prefill(ServiceType type, const std::string& id) {
    std::vector<ServiceType> all = ServiceTypeHelper::all();
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i] == type) serviceIndex_ = static_cast<int>(i);
    }
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
}

D2D1_RECT_F LoginView::card() const {
    const float x = (bounds_.left + bounds_.right - kCardWidth) / 2;
    const float y = (bounds_.top + bounds_.bottom - kCardHeight) / 2 - 12;
    return D2D1::RectF(x, y, x + kCardWidth, y + kCardHeight);
}

D2D1_RECT_F LoginView::field(int row) const {
    const D2D1_RECT_F c = card();
    const float top = c.top + (row == 0 ? 272.0f : 354.0f);
    return D2D1::RectF(c.left + 36, top, c.right - 36, top + 46);
}

void LoginView::layout(const D2D1_RECT_F& bounds) {
    View::layout(bounds);
    HWND edits[] = { account_, password_ };
    for (int row = 0; row < 2; ++row) {
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
    c.text(L"腾", logo, 26, Theme::rgb(0xFFFFFF), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.text(L"腾*立体社交平台", D2D1::RectF(card.left, card.top + 110, card.right, card.top + 140), 22,
           Theme::rgb(Theme::TEXT), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.text(L"模拟即时通信系统 · 图形界面版", D2D1::RectF(card.left, card.top + 142, card.right, card.top + 164), 13,
           Theme::rgb(Theme::TEXT3), Align::CENTER);

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

    // 账号、密码
    const wchar_t* labels[] = { L"账号", L"密码" };
    HWND edits[] = { account_, password_ };
    for (int row = 0; row < 2; ++row) {
        const D2D1_RECT_F f = field(row);
        c.text(labels[row], D2D1::RectF(f.left + 2, f.top - 22, f.right, f.top - 4), 13, Theme::rgb(Theme::TEXT2));
        c.fillRound(f, 12, Theme::rgb(Theme::FIELD));
        if (GetFocus() == edits[row]) c.strokeRound(f, 12, accent, 1.5f);
        addHit(f, row == 0 ? ACT_ACCOUNT : ACT_PASSWORD);
    }
    if (!error_.empty())
        c.text(error_, D2D1::RectF(card.left, card.top + 406, card.right, card.top + 428), 13, Theme::rgb(Theme::DANGER),
               Align::CENTER);
    else if (!notice_.empty())
        c.text(notice_, D2D1::RectF(card.left + 20, card.top + 406, card.right - 20, card.top + 428), 13, Theme::rgb(0x00A870),
               Align::CENTER);

    // 登录按钮
    const D2D1_RECT_F button = D2D1::RectF(card.left + 36, card.top + 434, card.right - 36, card.top + 482);
    c.fillRound(button, 12, hovered(button) ? Theme::darker(accent, 0.08f) : accent);
    c.text(L"登  录", button, 16, Theme::rgb(0xFFFFFF), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    addHit(button, ACT_LOGIN);

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
        GuiWindow::focus(edit == account_ ? password_ : account_);
        return true;
    }
    if (key == VK_RETURN) {
        if (edit == account_) GuiWindow::focus(password_);
        else window_.submitLoginLater();
        return true;
    }
    return false;
}

void LoginView::submit() {
    const std::string id = Util::trim(GuiText::utf8(GuiText::editText(account_)));
    const std::string password = Util::trim(GuiText::utf8(GuiText::editText(password_)));
    std::string msg;
    if (!window_.session().login(service(), id, password, msg)) {
        error_ = GuiText::wide(msg);
        SetWindowTextW(password_, L"");
        GuiWindow::focus(password_);
        window_.invalidate();
        return;
    }
    window_.showMain();
    if (!msg.empty()) window_.mainView().toast(GuiText::wide(msg));
}
