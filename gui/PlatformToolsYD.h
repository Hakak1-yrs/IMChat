#pragma once
#include <string>
#include "DateYD.h"

class DialogYD;
class GuiWindowYD;

// 登录页和主界面都要用到的几项平台功能（对应控制台主菜单的“注册新用户、平台概况、立即保存数据”
// 以及用户菜单的“开通新服务”），集中在这里，避免两处各写一份
class PlatformToolsYD {
public:
    static void showOverview(GuiWindowYD& window);       // 平台概况：所有用户及其服务、各服务的群数量
    static void saveNow(GuiWindowYD& window);            // 立即保存数据
    static void registerDialog(GuiWindowYD& window);     // 注册新用户
    static void openServiceDialog(GuiWindowYD& window);  // 为当前用户开通新服务

    // 读取对话框中从 first 开始的 5 个输入框：昵称、出生日期、所在地、密码、再次输入密码。
    // 检查规则与控制台版相同：不能为空、日期合法且不晚于今天、密码至少 6 位且两次一致；不合格时在对话框上显示原因
    static bool readProfile(DialogYD& dialog, size_t first, std::string& nickname, DateYD& birthday,
                            std::string& location, std::string& password);
    static void addProfileFields(DialogYD& dialog);       // 依次添加上述 5 个输入框
};
