#pragma once
#include <string>
#include "PageModelYD.h"

class GuiWindowYD;
class MainViewYD;
class PlatformYD;
class ServiceBaseYD;
class SessionYD;
class UserYD;

// 功能页抽象基类：个人中心、好友资料、跨服务好友、群设置、全部群、点对点聊天、实时群聊室都从它派生。
// 显示在主界面右侧，由 PagePanelYD 负责绘制：每次重绘时调用 build() 取得最新内容，
// 按钮被点击时调用 onAction()——页面与控制台版的一个子菜单相对应，操作都通过业务类的接口完成
class PageYD {
public:
    explicit PageYD(GuiWindowYD& window);
    virtual ~PageYD() = default;
    PageYD(const PageYD&) = delete;
    PageYD& operator=(const PageYD&) = delete;

    virtual void build(PageModelYD& model) = 0;
    virtual void onAction(int action, const std::string& arg) = 0;
    virtual void onCompose(const std::string& text);   // 页面底部输入框发送的内容（聊天页）
    virtual bool poll();                                // 网络页面定时处理收发，返回 true 表示有新内容需要重绘
    virtual std::string key() const;                    // 页面对应的会话（用于左侧列表高亮），如 "P:10002"

protected:
    SessionYD& session();
    PlatformYD& platform();
    ServiceBaseYD& me();          // 当前正在使用的服务账号
    UserYD& user();
    MainViewYD& main();
    void toast(const std::wstring& text);
    void refresh();               // 数据有变化：刷新左侧列表并重绘

    GuiWindowYD& window_;
};
