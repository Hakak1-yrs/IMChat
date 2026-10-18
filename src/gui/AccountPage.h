#pragma once
#include "Page.h"

// 个人中心（对应控制台“用户菜单 → 3. 个人信息”）：
// 显示当前服务的资料（调用虚函数 showInfo()，不同服务显示各自的特有信息）、好友列表和群列表；
// 修改昵称、所在地、出生日期、密码，绑定/解绑 QQ，开通新服务，关闭本服务，注销整个账号
class AccountPage : public Page {
public:
    explicit AccountPage(GuiWindow& window);
    void build(PageModel& model) override;
    void onAction(int action, const std::string& arg) override;
    std::string key() const override;

private:
    enum Action {
        ACT_NICKNAME = 1, ACT_LOCATION, ACT_BIRTHDAY, ACT_PASSWORD, ACT_BIND,
        ACT_OPEN_SERVICE, ACT_CLOSE_SERVICE, ACT_DELETE_ACCOUNT, ACT_FRIEND, ACT_GROUP
    };
    void editText(const std::wstring& title, const std::wstring& label, const std::string& current, bool nickname);
    void changeBirthday();
    void changePassword();
    void bindQQ();
    void closeService();
    void deleteAccount();
};
