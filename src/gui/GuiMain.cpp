// IMChat 图形客户端（Win32 + Direct2D 自绘界面）。
// 与控制台客户端共用全部业务类和 data 目录：两个客户端可以同时运行、互相收发消息。
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <sstream>
#include "DataDir.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "LoginView.h"
#include "Platform.h"

#include <dbghelp.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dbghelp.lib")

namespace {

// 程序崩溃时把调用栈（函数名、源文件和行号）写到 %TEMP%\IMChatClient_crash.log，便于定位问题
LONG WINAPI writeCrashLog(EXCEPTION_POINTERS* info) {
    wchar_t path[MAX_PATH];
    GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"IMChatClient_crash.log");
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"w") != 0 || !file) return EXCEPTION_CONTINUE_SEARCH;
    fprintf(file, "exception 0x%08lX at %p, thread %lu\n", info->ExceptionRecord->ExceptionCode,
            info->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(process, nullptr, TRUE);
    void* frames[64];
    const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
    for (USHORT i = 0; i < count; ++i) {
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);
        IMAGEHLP_LINE64 line{ sizeof(IMAGEHLP_LINE64) };
        DWORD offset = 0;
        const bool named = SymFromAddr(process, address, nullptr, symbol);
        const bool lined = SymGetLineFromAddr64(process, address, &offset, &line);
        fprintf(file, "#%02u %s  %s:%lu\n", i, named ? symbol->Name : "?", lined ? line.FileName : "?", lined ? line.LineNumber : 0);
    }
    fclose(file);
    return EXCEPTION_CONTINUE_SEARCH;
}

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
    SetUnhandledExceptionFilter(writeCrashLog);
    std::ostringstream log;
    std::streambuf* original = std::cout.rdbuf(log.rdbuf());
    int code = 0;
    {
        Platform platform(DataDir::find());   // 构造时从文件加载数据
        report(log.str(), L"数据检查", { "[警告]", "未找到" });
        log.str("");
        GuiWindow window(instance, platform);
        if (!window.create(showCommand)) {
            MessageBoxW(nullptr, L"窗口创建失败（Direct2D 初始化失败）。", L"IMChat", MB_ICONERROR);
            code = 1;
        } else {
            // 演示用：IMChatClient.exe [--server 127.0.0.1:9000] --login QQ 10002 123456 直接登录，
            // 方便同时打开几个窗口演示多人聊天；带 --server 时以联网模式登录
            int argc = 0;
            LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            std::string server;
            ServiceType type = ServiceType::QQ;
            for (int i = 1; argv && i < argc; ++i) {
                const std::wstring arg = argv[i];
                if (arg == L"--server" && i + 1 < argc) {
                    server = GuiText::utf8(argv[++i]);
                } else if (arg == L"--login" && i + 3 < argc && ServiceTypeHelper::parse(GuiText::utf8(argv[i + 1]), type)) {
                    const std::string id = GuiText::utf8(argv[i + 2]), password = GuiText::utf8(argv[i + 3]);
                    if (server.empty()) window.autoLogin(type, id, password);
                    else window.loginView().autoRemote(server, type, id, password);
                    i += 3;
                }
            }
            if (argv) LocalFree(argv);
            code = window.run();
        }
    }   // platform 析构时自动把数据写回文件
    std::cout.rdbuf(original);
    report(log.str(), L"保存数据", { "[注意]", "[错误]", "失败" });
    return code;
}
