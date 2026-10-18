#pragma once
#include <string>
#include "ServiceType.h"
#include "View.h"

// 登录页：居中的白色卡片，选择服务、输入账号密码。强调色随所选服务变化（QQ 蓝、微信绿……）。
// 右上角切换单机 / 联网：单机模式直接读写本地数据；联网模式多一个服务器地址，登录在后台进行（连接、登录、加载数据），
// 按钮上显示进度，完成后进入主界面
class LoginView : public View {
public:
    explicit LoginView(GuiWindow& window);

    void show() override;
    void hide() override;
    void layout(const D2D1_RECT_F& bounds) override;
    void paint(Canvas& canvas) override;
    void onTimer() override;
    bool onEditKey(HWND edit, int key) override;
    void submit();
    void setNotice(const std::wstring& text);                 // 卡片上显示一行提示（如“已注销”）
    void prefill(ServiceType type, const std::string& id);  // 注册成功后填好服务和账号
    // 演示用：启动参数 --server 地址 --login 服务 账号 密码 时直接以联网模式登录
    void autoRemote(const std::string& server, ServiceType type, const std::string& id, const std::string& password);

private:
    enum Action { ACT_SERVICE = 1, ACT_ACCOUNT, ACT_PASSWORD, ACT_LOGIN, ACT_LINK, ACT_MODE, ACT_SERVER };
    enum Link { LINK_REGISTER, LINK_OVERVIEW, LINK_SAVE };
    void onAction(int action, int index) override;
    D2D1_RECT_F card() const;
    D2D1_RECT_F field(int row) const;   // 0 账号，1 密码，2 服务器（联网模式）
    ServiceType service() const;
    void startRemote(const std::string& server, const std::string& id, const std::string& password);
    void remoteFailed(const std::wstring& error);
    void selectService(ServiceType type);

    HWND account_ = nullptr;
    HWND password_ = nullptr;
    HWND server_ = nullptr;
    std::wstring serverText_ = L"127.0.0.1:9000";   // 上次使用的服务器地址（回到登录页时保留）
    int serviceIndex_ = 0;
    bool remote_ = false;                // 联网模式
    bool connecting_ = false;            // 联网登录进行中
    unsigned long long connectStarted_ = 0;
    std::wstring error_;
    std::wstring notice_;
};
