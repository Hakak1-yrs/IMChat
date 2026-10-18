#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

// 图形界面的文字工具：业务类内部统一使用 UTF-8 字符串，而 Direct2D / 编辑框使用 UTF-16 宽字符串，
// 两者在界面层相互转换；另外负责界面上的时间显示格式
class GuiText {
public:
    static std::wstring wide(const std::string& utf8);
    static std::string utf8(const std::wstring& text);
    static std::wstring editText(HWND edit);
    static std::wstring initial(const std::string& name);  // 头像上显示的第一个字
    static std::wstring listTime(const std::string& time); // 会话列表：今天显示“时:分”，否则“月-日”
    static std::wstring chatTime(const std::string& time); // 聊天区的时间分隔：今天 / 今年 / 更早，详略不同
    static long long minutes(const std::string& time);     // “yyyy-mm-dd hh:mm:ss”换算成分钟数，用来比较间隔
    // 收集 action 执行期间向 std::cout 输出的文字，按行返回（去掉首尾空白和空行）。
    // 业务类的 showInfo()、showFeatures() 等虚函数把各自的特有信息输出到 cout，图形界面借此显示同样的内容
    static std::vector<std::string> capture(const std::function<void()>& action);
};
