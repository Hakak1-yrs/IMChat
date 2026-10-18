// IMChat 服务端。用法：IMChatServer [--port 9000] [--data 数据目录] [--idle-timeout 秒] [--verbose]
#include "ChatServer.h"   // 网络头文件（winsock2.h）必须在其他 Windows 头文件之前
#include <csignal>
#include <iostream>
#include <string>
#include <vector>
#include "DataDir.h"
#include "Log.h"
#include "Platform.h"
#include "Util.h"

#ifdef _WIN32
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

namespace {

EventLoop* g_loop = nullptr;

// 命令行参数统一转成 UTF-8（Windows 的 argv 是本地代码页，数据目录含中文时会乱码）
std::vector<std::string> arguments(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; wide && i < count; ++i) args.push_back(Util::narrow(wide[i]));
    if (wide) LocalFree(wide);
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return args;
}

#ifdef _WIN32
BOOL WINAPI onConsoleEvent(DWORD) {
    if (g_loop) g_loop->quit();   // 让 main 正常返回，Platform 析构时保存数据
    return TRUE;
}
#else
void onSignal(int) {
    if (g_loop) g_loop->quit();
}
#endif

void printUsage() {
    std::cout << "用法：IMChatServer [--port 端口，默认 9000] [--data 数据目录] [--idle-timeout 秒，默认 90] [--verbose]\n";
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    ChatServer::Options options;
    std::string dataDir;
    const std::vector<std::string> args = arguments(argc, argv);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const bool hasValue = i + 1 < args.size();
        try {
            if (args[i] == "--port" && hasValue) options.port = static_cast<std::uint16_t>(std::stoi(args[++i]));
            else if (args[i] == "--data" && hasValue) dataDir = args[++i];
            else if (args[i] == "--idle-timeout" && hasValue) options.idleTimeout = std::chrono::seconds(std::stoi(args[++i]));
            else if (args[i] == "--verbose") Log::setLevel(Log::Level::DEBUG);
            else {
                printUsage();
                return args[i] == "--help" ? 0 : 1;
            }
        } catch (const std::exception&) {
            printUsage();
            return 1;
        }
    }
    if (dataDir.empty()) dataDir = DataDir::find();
    else if (dataDir.back() != '\\' && dataDir.back() != '/') dataDir += '/';
    Log::info("数据目录：" + DataDir::absolute(dataDir));

    Platform platform(dataDir);
    EventLoop loop;
    ChatServer server(loop, platform, options);
    std::string error;
    if (!server.start(error)) {
        Log::error(error);
        return 1;
    }
    g_loop = &loop;
#ifdef _WIN32
    SetConsoleCtrlHandler(onConsoleEvent, TRUE);
#else
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#endif
    loop.loop();
    g_loop = nullptr;
    Log::info("服务端已停止，正在保存数据");
    return 0;   // 依次析构 server、loop、platform：platform 析构时写回数据文件
}
