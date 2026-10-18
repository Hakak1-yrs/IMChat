#pragma once
#include <string>
#include "ServiceTypeYD.h"
#include "ViewYD.h"

// 登录页：居中的白色卡片，选择服务、输入账号密码。强调色随所选服务变化（QQ 蓝、微信绿……）
class LoginViewYD : public ViewYD {
public:
    explicit LoginViewYD(GuiWindowYD& window);

    void show() override;
    void hide() override;
    void layout(const D2D1_RECT_F& bounds) override;
    void paint(CanvasYD& canvas) override;
    bool onEditKey(HWND edit, int key) override;
    void submit();
    void setNotice(const std::wstring& text);                 // 卡片上显示一行提示（如“已注销”）
    void prefill(ServiceTypeYD type, const std::string& id);  // 注册成功后填好服务和账号

private:
    enum ActionYD { ACT_SERVICE = 1, ACT_ACCOUNT, ACT_PASSWORD, ACT_LOGIN, ACT_LINK };
    enum LinkYD { LINK_REGISTER, LINK_OVERVIEW, LINK_SAVE };
    void onAction(int action, int index) override;
    D2D1_RECT_F card() const;
    D2D1_RECT_F field(int row) const;   // 0 账号，1 密码
    ServiceTypeYD service() const;

    HWND account_ = nullptr;
    HWND password_ = nullptr;
    int serviceIndex_ = 0;
    std::wstring error_;
    std::wstring notice_;
};
