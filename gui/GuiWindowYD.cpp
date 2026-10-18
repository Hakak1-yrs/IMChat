#include "GuiWindowYD.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <cmath>
#include "DialogYD.h"
#include "GuiTextYD.h"
#include "LoginViewYD.h"
#include "MainViewYD.h"
#include "ThemeYD.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
// 使用新版通用控件（输入框的占位提示文字需要它）
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

const wchar_t* kClassName = L"IMClientYDWindow";
const UINT_PTR kTimerId = 1;
const UINT kSubmitLogin = WM_APP + 1;   // 登录页按回车：推迟到消息循环中处理，避免在输入框自己的消息处理中销毁它
const UINT kRunTasks = WM_APP + 2;      // 执行 post() 推迟的任务

COLORREF toColorRef(unsigned hex) { return RGB((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF); }

}  // namespace

GuiWindowYD::GuiWindowYD(HINSTANCE instance, PlatformYD& platform)
    : instance_(instance), platform_(platform), session_(platform) {}

GuiWindowYD::~GuiWindowYD() {
    for (auto& kv : brushes_) DeleteObject(kv.second);
    if (editFont_) DeleteObject(editFont_);
}

bool GuiWindowYD::create(int showCommand) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    const UINT dpi = GetDpiForSystem();
    const int width = MulDiv(1120, dpi, 96), height = MulDiv(740, dpi, 96);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + (work.right - work.left - width) / 2, y = work.top + (work.bottom - work.top - height) / 2;
    hwnd_ = CreateWindowExW(0, kClassName, L"腾*立体社交", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height,
                            nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    // Windows 11：标题栏改成与界面一致的浅色（Windows 10 不支持，调用失败也没有影响）
    COLORREF caption = toColorRef(ThemeYD::PANEL);
    DwmSetWindowAttribute(hwnd_, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));

    login_ = std::make_unique<LoginViewYD>(*this);
    main_ = std::make_unique<MainViewYD>(*this);
    showLogin();
    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
}

int GuiWindowYD::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void GuiWindowYD::showLogin() {
    setTitle(L"腾*立体社交 · 登录");
    switchTo(login_.get());
}

void GuiWindowYD::showMain() { switchTo(main_.get()); }

bool GuiWindowYD::autoLogin(ServiceTypeYD type, const std::string& id, const std::string& password) {
    std::string msg;
    if (!session_.login(type, id, password, msg)) return false;
    showMain();
    if (!msg.empty()) main_->toast(GuiTextYD::wide(msg));
    return true;
}

void GuiWindowYD::switchTo(ViewYD* view) {
    if (dialog_) {   // 切换页面（如注销账号回到登录页）时，关掉还开着的对话框
        dialog_->hide();
        dialog_.reset();
        hiddenEdits_.clear();
    }
    if (view_) view_->hide();
    view_ = view;
    view_->show();
    relayout();
    invalidate();
}

void GuiWindowYD::relayout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const D2D1_RECT_F bounds = D2D1::RectF(0, 0, rc.right / scale(), rc.bottom / scale());
    if (view_) view_->layout(bounds);
    if (dialog_) {
        dialog_->layout(bounds);
        hideViewEdits();
    }
}

void GuiWindowYD::invalidate() { InvalidateRect(hwnd_, nullptr, FALSE); }

void GuiWindowYD::submitLoginLater() { PostMessageW(hwnd_, kSubmitLogin, 0, 0); }

void GuiWindowYD::post(std::function<void()> task) {
    tasks_.push_back(std::move(task));
    PostMessageW(hwnd_, kRunTasks, 0, 0);
}

void GuiWindowYD::runTasks() {
    while (!tasks_.empty()) {
        std::function<void()> task = std::move(tasks_.front());
        tasks_.pop_front();
        task();
    }
}

void GuiWindowYD::openDialog(std::unique_ptr<DialogYD> dialog) {
    std::shared_ptr<DialogYD> next(dialog.release());
    post([this, next] {
        if (dialog_) dialog_->hide();   // 从一个对话框直接换成另一个（例如操作成功后的提示）
        dialog_ = next;
        dialog_->show();
        relayout();
        invalidate();
    });
}

void GuiWindowYD::closeDialog(DialogYD* which) {
    post([this, which] {
        if (!dialog_ || dialog_.get() != which) return;
        dialog_->hide();
        dialog_.reset();
        for (HWND edit : hiddenEdits_) {
            if (IsWindow(edit) && editColors_.count(edit)) ShowWindow(edit, SW_SHOW);
        }
        hiddenEdits_.clear();
        relayout();   // 各视图重新决定自己的输入框是否显示
        invalidate();
    });
}

bool GuiWindowYD::hasDialog() const { return dialog_ != nullptr; }

void GuiWindowYD::info(const std::wstring& title, const std::wstring& text) {
    auto dialog = std::make_unique<DialogYD>(*this, title);
    dialog->message(text).cancelText(L"知道了");
    openDialog(std::move(dialog));
}

void GuiWindowYD::hideViewEdits() {
    for (auto& kv : editColors_) {
        HWND edit = kv.first;
        if (dialog_->owns(edit) || !IsWindowVisible(edit)) continue;
        ShowWindow(edit, SW_HIDE);
        hiddenEdits_.push_back(edit);
    }
}

void GuiWindowYD::setTitle(const std::wstring& title) { SetWindowTextW(hwnd_, title.c_str()); }

HWND GuiWindowYD::hwnd() const { return hwnd_; }

float GuiWindowYD::scale() const { return dpi_ / 96.0f; }

CanvasYD& GuiWindowYD::canvas() { return canvas_; }

SessionYD& GuiWindowYD::session() { return session_; }

PlatformYD& GuiWindowYD::platform() { return platform_; }

MainViewYD& GuiWindowYD::mainView() { return *main_; }

LoginViewYD& GuiWindowYD::loginView() { return *login_; }

HWND GuiWindowYD::createEdit(bool multiline, bool password, const std::wstring& placeholder, unsigned background) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    style |= multiline ? (ES_MULTILINE | ES_AUTOVSCROLL) : ES_AUTOHSCROLL;
    if (password) style |= ES_PASSWORD;
    HWND edit = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 10, 10, hwnd_, nullptr, instance_, nullptr);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
    SendMessageW(edit, EM_SETLIMITTEXT, 250, 0);   // 消息最多 250 个字，与业务层的限制一致
    if (!placeholder.empty() && !multiline)
        SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(placeholder.c_str()));
    editColors_[edit] = toColorRef(background);
    SetWindowSubclass(edit, editProc, 1, reinterpret_cast<DWORD_PTR>(this));
    return edit;
}

void GuiWindowYD::destroyEdit(HWND& edit) {
    if (!edit) return;
    editColors_.erase(edit);
    DestroyWindow(edit);
    edit = nullptr;
}

void GuiWindowYD::placeEdit(HWND edit, const D2D1_RECT_F& r) {
    const float s = scale();
    const int left = static_cast<int>(std::lround(r.left * s)), top = static_cast<int>(std::lround(r.top * s));
    SetWindowPos(edit, nullptr, left, top, static_cast<int>(std::lround(r.right * s)) - left,
                 static_cast<int>(std::lround(r.bottom * s)) - top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void GuiWindowYD::focus(HWND edit) {
    if (!edit) return;
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);
}

void GuiWindowYD::updateEditFont() {
    HFONT old = editFont_;
    editFont_ = CreateFontW(-static_cast<int>(std::lround(15 * scale())), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                            L"Microsoft YaHei UI");
    for (auto& kv : editColors_) SendMessageW(kv.first, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
    if (old) DeleteObject(old);
}

LRESULT CALLBACK GuiWindowYD::windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<GuiWindowYD*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<GuiWindowYD*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

// 输入框的消息先经过这里：回车、Tab、Esc 交给当前视图处理（发送消息、切换输入框等）
LRESULT CALLBACK GuiWindowYD::editProc(HWND edit, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    auto* self = reinterpret_cast<GuiWindowYD*>(ref);
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN || wp == VK_TAB || wp == VK_ESCAPE) {
            std::shared_ptr<DialogYD> dialog = self->dialog_;   // 先持有一份，处理过程中对话框不会被销毁
            if (dialog && dialog->owns(edit)) {
                dialog->onEditKey(edit, static_cast<int>(wp));
                return 0;
            }
            if (self->view_ && self->view_->onEditKey(edit, static_cast<int>(wp))) return 0;
        }
        break;
    case WM_CHAR:
        if (wp == L'\r' || wp == L'\n' || wp == L'\t' || wp == 27) return 0;   // 不插入换行，也不发出提示音
        if (wp == 1) {   // Ctrl+A 全选
            SendMessageW(edit, EM_SETSEL, 0, -1);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(edit, editProc, id);
        break;
    }
    return DefSubclassProc(edit, msg, wp, lp);
}

LRESULT GuiWindowYD::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
        if (!canvas_.init(hwnd_, dpi_)) return -1;
        updateEditFont();
        SetTimer(hwnd_, kTimerId, 100, nullptr);   // 每 0.1 秒：处理 TCP 网络收发；主界面每隔几次检查一下新消息
        return 0;
    case WM_SIZE:
        canvas_.resize();
        if (view_) relayout();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        if (view_ && canvas_.begin()) {
            view_->paint(canvas_);
            if (dialog_) dialog_->paint(canvas_);   // 对话框画在最上层
            canvas_.end();
        }
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;   // 背景由 Direct2D 绘制，跳过系统擦除，避免闪烁
    case WM_MOUSEMOVE:
        if (!trackingMouse_) {
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd_, 0 };
            TrackMouseEvent(&track);
            trackingMouse_ = true;
        }
        if (ViewYD* target = dialog_ ? static_cast<ViewYD*>(dialog_.get()) : view_)
            target->onMouseMove(GET_X_LPARAM(lp) / scale(), GET_Y_LPARAM(lp) / scale());
        return 0;
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        if (ViewYD* target = dialog_ ? static_cast<ViewYD*>(dialog_.get()) : view_) target->onMouseMove(-1, -1);
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd_);   // 点击空白处时输入框失去焦点
        std::shared_ptr<DialogYD> dialog = dialog_;   // 对话框打开时，点击只交给对话框
        if (dialog) dialog->onClick(GET_X_LPARAM(lp) / scale(), GET_Y_LPARAM(lp) / scale());
        else if (view_) view_->onClick(GET_X_LPARAM(lp) / scale(), GET_Y_LPARAM(lp) / scale());
        return 0;
    }
    case WM_RBUTTONDOWN:
        if (!dialog_ && view_) view_->onRightClick(GET_X_LPARAM(lp) / scale(), GET_Y_LPARAM(lp) / scale());
        return 0;
    case WM_MOUSEWHEEL: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd_, &p);
        if (ViewYD* target = dialog_ ? static_cast<ViewYD*>(dialog_.get()) : view_)
            target->onWheel(p.x / scale(), p.y / scale(), GET_WHEEL_DELTA_WPARAM(wp) / 120.0f);
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && reinterpret_cast<HWND>(wp) == hwnd_) {
            ViewYD* target = dialog_ ? static_cast<ViewYD*>(dialog_.get()) : view_;
            SetCursor(LoadCursor(nullptr, target && target->overClickable() ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_TIMER:
        if (view_) view_->onTimer();
        return 0;
    case kRunTasks:
        runTasks();
        return 0;
    case WM_KEYDOWN:
        if (dialog_ && (wp == VK_RETURN || wp == VK_ESCAPE)) {   // 没有输入框的对话框：回车确认，Esc 取消
            std::shared_ptr<DialogYD> dialog = dialog_;
            if (wp == VK_RETURN) dialog->accept();
            else dialog->cancel();
            return 0;
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE && view_ && !(dialog_ && dialog_->owns(reinterpret_cast<HWND>(lp))))
            view_->onEditChange(reinterpret_cast<HWND>(lp));
        else if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) invalidate();   // 输入框的焦点高亮
        return 0;
    case WM_CTLCOLOREDIT: {
        auto it = editColors_.find(reinterpret_cast<HWND>(lp));
        const COLORREF bg = it == editColors_.end() ? RGB(255, 255, 255) : it->second;
        HDC dc = reinterpret_cast<HDC>(wp);
        SetBkColor(dc, bg);
        SetTextColor(dc, toColorRef(ThemeYD::TEXT));
        HBRUSH& brush = brushes_[bg];
        if (!brush) brush = CreateSolidBrush(bg);
        return reinterpret_cast<LRESULT>(brush);
    }
    case kSubmitLogin:
        if (view_ == login_.get()) login_->submit();
        return 0;
    case WM_DPICHANGED: {   // 窗口被拖到另一块缩放比例不同的显示器上
        dpi_ = static_cast<float>(HIWORD(wp));
        canvas_.setDpi(dpi_);
        updateEditFont();
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        relayout();
        invalidate();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize.x = static_cast<LONG>(940 * scale());
        info->ptMinTrackSize.y = static_cast<LONG>(660 * scale());
        return 0;
    }
    case WM_ENDSESSION:   // 关机或注销 Windows 时程序会被直接结束，先保存数据
        if (wp) platform_.saveOnExit();
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd_, kTimerId);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
