#pragma once
#include <windows.h>
#include <d2d1.h>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Canvas.h"
#include "LocalSession.h"
#include "RemoteAccounts.h"

class View;
class Dialog;
class LoginView;
class MainView;

// 主窗口：负责 Win32 窗口本身——创建窗口、消息循环、高分屏缩放、输入框子控件，
// 并把绘制和鼠标键盘事件转交给当前视图（登录页或主界面）
class GuiWindow {
public:
    GuiWindow(HINSTANCE instance, Platform& platform);
    ~GuiWindow();
    GuiWindow(const GuiWindow&) = delete;
    GuiWindow& operator=(const GuiWindow&) = delete;

    bool create(int showCommand);
    int run();                                   // 消息循环，窗口关闭后返回
    void showLogin();
    void showMain();
    // 演示用：启动参数 --login 服务 账号 密码 时直接登录（方便同时打开几个窗口演示多人聊天）
    bool autoLogin(ServiceType type, const std::string& id, const std::string& password);
    // 联网模式：登录页创建 RemoteAccounts 并开始连接，连接、登录、加载数据完成后由登录页切换到主界面
    void startRemote(std::unique_ptr<RemoteAccounts> accounts);
    void endRemote();                            // 退出联网模式（退出登录、登录失败、被挤下线）

    void invalidate();
    void submitLoginLater();                     // 回车登录：放到消息循环里执行（登录成功会销毁正在处理按键的输入框）
    // 推迟到消息循环中执行：切换页面、打开或关闭对话框都通过它进行，
    // 避免在某个对象自己的点击处理函数还没返回时就把它销毁
    void post(std::function<void()> task);
    void openDialog(std::unique_ptr<Dialog> dialog);
    void closeDialog(Dialog* which);           // which 不是当前对话框时不做任何事
    bool hasDialog() const;
    void info(const std::wstring& title, const std::wstring& text);   // 只有“知道了”按钮的提示框
    void setTitle(const std::wstring& title);
    HWND hwnd() const;
    float scale() const;                         // 当前 DPI / 96
    Canvas& canvas();
    Session& session();                          // 当前使用的会话：联网模式为 RemoteSession，否则为 LocalSession
    LocalSession& localSession();                // 单机模式的会话（只在单机模式下开放的功能页使用）
    RemoteSession* remoteSession();              // 联网模式下当前服务的会话，单机模式为空
    RemoteAccounts* remoteAccounts();            // 联网模式下本人的全部服务，单机模式为空
    Platform& platform();
    MainView& mainView();
    LoginView& loginView();

    // ---- 输入框：系统的编辑框控件放在画出来的圆角输入区域里，看起来与界面融为一体 ----
    HWND createEdit(bool multiline, bool password, const std::wstring& placeholder, unsigned background);
    void destroyEdit(HWND& edit);
    void placeEdit(HWND edit, const D2D1_RECT_F& dipRect);   // DIP 坐标 → 像素
    static void focus(HWND edit);

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK editProc(HWND edit, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR self);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void switchTo(View* view);
    void relayout();
    void updateEditFont();
    void hideViewEdits();                        // 对话框打开期间，把界面上其他输入框暂时隐藏（它们是系统控件，会盖住对话框）
    void runTasks();

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    Platform& platform_;
    LocalSession local_;
    std::unique_ptr<RemoteAccounts> remote_;     // 联网模式：本人的全部服务账号
    Canvas canvas_;
    float dpi_ = 96.0f;
    HFONT editFont_ = nullptr;
    std::map<HWND, COLORREF> editColors_;
    std::map<COLORREF, HBRUSH> brushes_;
    std::unique_ptr<LoginView> login_;
    std::unique_ptr<MainView> main_;
    View* view_ = nullptr;
    std::shared_ptr<Dialog> dialog_;
    std::vector<HWND> hiddenEdits_;
    std::deque<std::function<void()>> tasks_;
    bool trackingMouse_ = false;
};
