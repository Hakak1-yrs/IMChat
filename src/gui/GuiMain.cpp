// IMChat 图形客户端（Win32 + Direct2D 自绘界面）。
// 与控制台客户端共用全部业务类和 data 目录：两个客户端可以同时运行、互相收发消息。
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <sstream>
#include "DataDir.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "Platform.h"

#pragma comment(lib, "shell32.lib")

namespace {

bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// 业务类会向 std::cout 输出提示（如数据检查、保存结果）。图形界面没有控制台，有重要提示时弹窗显示
void report(const std::string& log, const wchar_t* title, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (contains(log, key)) {
            MessageBoxW(nullptr, GuiText::wide(log).c_str(), title, MB_ICONWARNING | MB_OK);
            return;
        }
    }
}

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);   // 高分屏下清晰显示
    std::ostringstream log;
    std::streambuf* original = std::cout.rdbuf(log.rdbuf());
    int code = 0;
    {
        Platform platform(DataDir::find());   // 构造时从文件加载数据
        report(log.str(), L"数据检查", { "[警告]", "未找到" });
        log.str("");
        GuiWindow window(instance, platform);
        if (!window.create(showCommand)) {
            MessageBoxW(nullptr, L"窗口创建失败（Direct2D 初始化失败）。", L"腾*立体社交", MB_ICONERROR);
            code = 1;
        } else {
            // 演示用：IMChatClient.exe --login QQ 10002 123456 直接登录，方便同时打开几个窗口演示多人聊天
            int argc = 0;
            LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            ServiceType type = ServiceType::QQ;
            if (argv && argc >= 5 && std::wstring(argv[1]) == L"--login" &&
                ServiceTypeHelper::parse(GuiText::utf8(argv[2]), type))
                window.autoLogin(type, GuiText::utf8(argv[3]), GuiText::utf8(argv[4]));
            if (argv) LocalFree(argv);
            code = window.run();
        }
    }   // platform 析构时自动把数据写回文件
    std::cout.rdbuf(original);
    report(log.str(), L"保存数据", { "[注意]", "[错误]", "失败" });
    return code;
}
