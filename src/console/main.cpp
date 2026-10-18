// IMChat 控制台客户端（最初是面向对象程序设计课程设计）
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include "DataDir.h"
#include "Menu.h"
#include "Platform.h"

static Platform* g_platform = nullptr;

// 直接点击窗口 × 或按 Ctrl+C 时，main 中的对象来不及析构，因此在这里先保存数据
static BOOL WINAPI onConsoleClose(DWORD type) {
    if (g_platform && (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT)) {
        g_platform->saveOnExit();
    }
    return FALSE;   // 交给系统默认处理（结束程序）
}

int main() {
    // 程序内部、数据文件和网络通信统一使用 UTF-8；控制台也切换到 UTF-8 输出
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    const std::string dataDir = DataDir::find();
    std::cout << "[系统] 数据目录：" << DataDir::absolute(dataDir) << "\n";
    Platform platform(dataDir);   // 构造时从文件加载数据（并检查数据是否前后一致）
    g_platform = &platform;
    SetConsoleCtrlHandler(onConsoleClose, TRUE);
    Menu menu(platform);
    try {
        menu.run();
    } catch (const std::exception& e) {
        std::cout << "\n程序结束：" << e.what() << "\n";
    }
    SetConsoleCtrlHandler(onConsoleClose, FALSE);
    g_platform = nullptr;
    return 0;   // platform 析构时自动把数据写回文件
}
